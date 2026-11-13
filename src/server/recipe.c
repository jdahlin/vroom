/* make's recipes for vsh: the callbacks rsh.c uses to run tools as requests here. */
#include "vccd-internal.h"

/* a shim of ours: vcc, vas, vld or var next to vccd */
static bool sh_is_tool(void *ctx, const char *argv0)
{
	const char *b = strrchr(argv0, '/');
	char dir[4096], real[4096];
	size_t n;

	(void)ctx;
	if (!b || !*tool_dir || (strcmp(b + 1, "vcc") && strcmp(b + 1, "vas") && strcmp(b + 1, "vld") &&
				 strcmp(b + 1, "var") && strcmp(b + 1, "vcc-nm") && strcmp(b + 1, "vcc-objcopy")))
		return false;
	n = b - argv0;
	if (n == strlen(tool_dir) && !memcmp(argv0, tool_dir, n))
		return true;
	if (n >= sizeof(dir))
		return false;
	memcpy(dir, argv0, n);
	dir[n] = 0;
	return realpath(dir, real) && !strcmp(real, tool_dir);
}

struct sh_ctx {
	const struct req *r;	/* the vsh request: cwd and env */
};

/* per thread: the request a recipe's tool call is turned into */
static _Thread_local char *sh_raw;
static _Thread_local char **sh_strs;

static int sh_run_tool(void *ctx, char **argv, char **assign, const int fds[3])
{
	const struct req *r = ((struct sh_ctx *)ctx)->r;
	struct vroom_req_hdr *h;
	struct req q;
	const char *kind = "bad";
	double t0 = now_ms();
	char *p, *end;
	int st, argc = 0, envc = 0;

	if (!sh_raw) {
		sh_raw = mmap(NULL, sizeof(struct vroom_req_hdr) + VROOM_MAX_PAYLOAD,
			      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		sh_strs = mmap(NULL, MAX_STRS * sizeof(char *), PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANON, -1, 0);
		if (sh_raw == MAP_FAILED || sh_strs == MAP_FAILED) {
			sh_raw = NULL;
			say(r, "%s: error: %s\n", "out of memory");
			return 127;
		}
	}
	h = (struct vroom_req_hdr *)sh_raw;
	p = sh_raw + sizeof(*h);
	end = p + VROOM_MAX_PAYLOAD;
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
	h->magic = VROOM_MAGIC;
	h->argc = argc;
	h->envc = envc;
	h->payload_len = p - (sh_raw + sizeof(*h));
	if (!split_req(sh_raw, sh_strs, &q)) {
		say(r, "%s: error: %s\n", "bad tool call in recipe");
		return 127;
	}
	memcpy(q.fds, fds, sizeof(q.fds));
	st = handle(&q, &kind);
	log_req(&q, kind, st, t0);
	return st;
big:
	say(r, "%s: error: %s\n", "recipe tool call too large");
	return 127;
}

static int sh_fixdep(void *ctx, const char *dep, const char *target, const char *cmd,
		     const char *cmdfile, int errfd)
{
	return vfixdep(((struct sh_ctx *)ctx)->r->cwd, dep, target, cmd, cmdfile, errfd);
}

/* argv: sh -c SCRIPT or sh -ec SCRIPT (make's .SHELLFLAGS); anything else is the shell's */
VPRIV int run_sh(const struct req *r)
{
	struct sh_ctx ctx = { r };
	struct rsh_ops ops = { &ctx, sh_is_tool, sh_run_tool, sh_fixdep };

	if (r->argc != 3 || (strcmp(r->argv[1], "-c") && strcmp(r->argv[1], "-ec")))
		return RSH_DECLINE;
	return rsh_run(r->cwd, r->argv[2], r->argv[1][1] == 'e', r->fds, &ops);
}
