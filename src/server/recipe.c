/*
 * recipe.c: the callbacks utils/shell.c needs to run make's recipes inside the server (the `vsh`
 * shim, argv[0] sh).
 *
 * rsh interprets the subset of sh that kbuild recipes use; every tool call in a recipe that names one
 * of our shims (vcc, vas, vld, var, vcc-nm, vcc-objcopy in vccd_tool_directory) is turned into a request
 * (vccd_recipe_tool_run builds the raw bytes in per-thread buffers, with the recipe's NAME=VALUE assignments
 * first in the environment) and run through vccd_request_handle() in this thread, with the recipe's fds.  Anything
 * rsh does not know makes vccd_recipe_run return SHELL_DECLINE and the shim runs the real shell.  A tool call
 * too large for VCCD_PROTOCOL_PAYLOAD_MAX fails the recipe with 127.
 */
#include "vccd-internal.h"

/* a shim of ours: vcc, vas, vld or var next to vccd */
static bool vccd_recipe_is_tool(void *ctx, const char *argv0)
{
	const char *b = strrchr(argv0, '/');
	char dir[4096], real[4096];
	size_t n;

	(void)ctx;
	if (!b || !*vccd_tool_directory || (strcmp(b + 1, "vcc") && strcmp(b + 1, "vas") && strcmp(b + 1, "vld") &&
				 strcmp(b + 1, "var") && strcmp(b + 1, "vcc-nm") && strcmp(b + 1, "vcc-objcopy")))
		return false;
	n = b - argv0;
	if (n == strlen(vccd_tool_directory) && !memcmp(argv0, vccd_tool_directory, n))
		return true;
	if (n >= sizeof(dir))
		return false;
	memcpy(dir, argv0, n);
	dir[n] = 0;
	return realpath(dir, real) && !strcmp(real, vccd_tool_directory);
}

struct vccd_recipe_context {
	const struct vccd_request *request;	/* the vsh request: cwd and env */
};

/* per thread: the request a recipe's tool call is turned into */
static _Thread_local char *vccd_recipe_request_buffer;
static _Thread_local char **vccd_recipe_request_strings;

static int vccd_recipe_tool_run(void *ctx, char **argv, char **assign, const int fds[3])
{
	const struct vccd_request *r = ((struct vccd_recipe_context *)ctx)->request;
	struct vccd_request_header *h;
	struct vccd_request q;
	const char *kind = "bad";
	double t0 = vccd_clock_milliseconds();
	char *p, *end;
	int st, argc = 0, envc = 0;

	if (!vccd_recipe_request_buffer) {
		vccd_recipe_request_buffer = mmap(NULL, sizeof(struct vccd_request_header) + VCCD_PROTOCOL_PAYLOAD_MAX,
			      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		vccd_recipe_request_strings = mmap(NULL, VCCD_STRINGS_MAX * sizeof(char *), PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANON, -1, 0);
		if (vccd_recipe_request_buffer == MAP_FAILED || vccd_recipe_request_strings == MAP_FAILED) {
			vccd_recipe_request_buffer = NULL;
			vccd_request_message_write(r, "%s: error: %s\n", "out of memory");
			return 127;
		}
	}
	h = (struct vccd_request_header *)vccd_recipe_request_buffer;
	p = vccd_recipe_request_buffer + sizeof(*h);
	end = p + VCCD_PROTOCOL_PAYLOAD_MAX;
#define PUT(s) do { size_t n_ = strlen(s) + 1; if (p + n_ > end) goto big; memcpy(p, s, n_); p += n_; } while (0)
	PUT(r->cwd);
	for (; argv[argc]; argc++)
		PUT(argv[argc]);
	/* the recipe's NAME=VALUE first, then the shell's environment without those names */
	for (char **a = assign; *a; a++, envc++)
		PUT(*a);
	for (char **e = r->envp; *e; e++) {
		bool dup = false;

		for (char **a = assign; *a && !dup; a++) {
			size_t n = strchr(*a, '=') - *a + 1;

			dup = !strncmp(*e, *a, n);
		}
		if (!dup) {
			PUT(*e);
			envc++;
		}
	}
#undef PUT
	h->magic = VCCD_PROTOCOL_MAGIC;
	h->argc = argc;
	h->envc = envc;
	h->payload_len = p - (vccd_recipe_request_buffer + sizeof(*h));
	if (!vccd_request_split(vccd_recipe_request_buffer, vccd_recipe_request_strings, &q)) {
		vccd_request_message_write(r, "%s: error: %s\n", "bad tool call in recipe");
		return 127;
	}
	memcpy(q.fds, fds, sizeof(q.fds));
	st = vccd_request_handle(&q, &kind);
	vccd_request_log(&q, kind, st, t0);
	return st;
big:
	vccd_request_message_write(r, "%s: error: %s\n", "recipe tool call too large");
	return 127;
}

static int vccd_recipe_fixdep_run(void *ctx, const char *dep, const char *target, const char *cmd,
		     const char *cmdfile, int errfd)
{
	return fixdep_run(((struct vccd_recipe_context *)ctx)->request->cwd, dep, target, cmd, cmdfile, errfd);
}

/* argv: sh -c SCRIPT or sh -ec SCRIPT (make's .SHELLFLAGS); anything else is the shell's */
VPRIV int vccd_recipe_run(const struct vccd_request *r)
{
	struct vccd_recipe_context ctx = { r };
	struct shell_callbacks ops = { &ctx, vccd_recipe_is_tool, vccd_recipe_tool_run, vccd_recipe_fixdep_run };

	if (r->argc != 3 || (strcmp(r->argv[1], "-c") && strcmp(r->argv[1], "-ec")))
		return SHELL_DECLINE;
	return shell_run(r->cwd, r->argv[2], r->argv[1][1] == 'e', r->fds, &ops);
}
