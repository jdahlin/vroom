/*
 * probe.c: recognise toolchain probes (vccd_probe_kind_classify) and answer them from the capability table
 * without compiling anything (vccd_probe_cc_answer).
 *
 * Kconfig and kbuild try dozens of options and snippets on every configuration; each answer is a
 * function of the options (capabilities.c) or of the snippet read from stdin, so no process or compile is
 * needed.  The kinds are described below.  The rule for recognising a probe is strict: anything that
 * reads a real file or writes a real output is a real job, because answering a real compile
 * with a probe's "yes" would produce no object.  A source that is not in the table is compiled for
 * real (forwarded with the bytes already read).  Answers must stay what clang would say: the table's
 * contents are what the kernel's configuration then enables.
 */
#include "vccd-internal.h"

/*
 * Probes: jobs whose answer vcc's capability table gives without compiling.
 *
 *   VCCD_PROBE_CC    -c/-S of C from /dev/null (cc-option, Kconfig's cc-probe):
 *               the answer depends on the options alone;
 *   VCCD_PROBE_AS    -c of assembler-with-cpp from stdin into /dev/null or a
 *               try-run temporary (as-instr),
 *   VCCD_PROBE_SOURCE   -c/-S of C from stdin into the same (`echo ... | $(CC)`):
 *               the source is looked up, and compiled for real (forwarded
 *               with the bytes already read) if it is not in the table;
 *   VCCD_PROBE_LINK  a link of C from stdin into /dev/null (cc-can-link.sh).
 *
 * Anything else (a real file, stdin into a real output) is a real job.
 */

static bool vccd_probe_output_is_discard(const char *out)
{
	return out && (!strcmp(out, "/dev/null") || strstr(out, ".tmp_"));
}

VPRIV int vccd_probe_kind_classify(const struct req *r)
{
	bool compile = false, devnull = false, in_stdin = false;
	const char *lang = "c", *out = NULL;
	int i;

	for (i = 1; i < r->argc; i++) {
		const char *a = r->argv[i];
		const char *why;
		int n;

		if (!strcmp(a, "-c") || !strcmp(a, "-S"))
			compile = true;
		if (!strcmp(a, "-E"))
			return VCCD_PROBE_NONE;
		if (!strcmp(a, "--param") || !strcmp(a, "-mllvm")) {
			i++;	/* its value is not an input; vccd_probe_cc_answer rejects the option */
			continue;
		}
		if (!strcmp(a, "-")) {
			if (in_stdin)
				return VCCD_PROBE_NONE;
			in_stdin = true;
			continue;
		}
		if (!strcmp(a, "--param") && i + 1 < r->argc) {
			i++;	/* its value is no input (vccd_probe_cc_answer rejects --param, as clang: unused) */
			continue;
		}
		if (a[0] != '-') {
			if (strcmp(a, "/dev/null"))
				return VCCD_PROBE_NONE;
			devnull = true;
			continue;
		}
		n = vccd_capabilities_cc_option_arity(a, i + 1 < r->argc ? r->argv[i + 1] : NULL, &why);
		if (n == 2) {
			if (!strcmp(a, "-x"))
				lang = r->argv[i + 1];
			if (!strcmp(a, "-o"))
				out = r->argv[i + 1];
			i++;
		}
	}
	if (devnull && !in_stdin && compile && !strcmp(lang, "c"))
		return VCCD_PROBE_CC;
	if (in_stdin && !devnull && compile && !strcmp(lang, "assembler-with-cpp") &&
	    vccd_probe_output_is_discard(out))
		return VCCD_PROBE_AS;
	if (in_stdin && !devnull && compile && !strcmp(lang, "c") && vccd_probe_output_is_discard(out))
		return VCCD_PROBE_SOURCE;
	if (in_stdin && !devnull && !compile && !strcmp(lang, "c") && out &&
	    !strcmp(out, "/dev/null"))
		return VCCD_PROBE_LINK;
	return VCCD_PROBE_NONE;
}

/* the probe's stdin, up to sizeof(buf); -1 if it is larger or unreadable */
static ssize_t vccd_probe_stdin_read(const struct req *r, char *buf, size_t size)
{
	size_t n = 0;

	for (;;) {
		ssize_t k = read(r->fds[0], buf + n, size - n);

		if (k < 0 && errno == EINTR)
			continue;
		if (k < 0)
			return -1;
		if (k == 0)
			return n;
		n += k;
		if (n == size)
			return -1;
	}
}

VPRIV int forward(const struct req *r, const char *sock);

/* all of fd into a malloc'ed *buf (*len bytes); nonzero on a read error */
VPRIV int vccd_fd_read_all(int fd, char **buf, size_t *len)
{
	size_t cap = 0, n = 0;
	char *b = NULL;

	for (;;) {
		ssize_t k;

		if (cap - n < 65536) {
			char *nb = realloc(b, cap = cap ? cap * 2 : 1 << 16);

			if (!nb)
				break;
			b = nb;
		}
		k = read(fd, b + n, cap - n);
		if (k < 0 && errno == EINTR)
			continue;
		if (k <= 0) {
			*buf = b, *len = n;
			return k < 0;
		}
		n += (size_t)k;
	}
	*buf = b, *len = n;
	return 1;
}

/* compile r for real with src[0..n) (already read from its stdin) as stdin:
   vcc, or the backend if there is one */
VPRIV int vccd_probe_stdin_compile(const struct req *r, const char *src, size_t n)
{
	struct req r2 = *r;
	driver_environment e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = r->fds[2], .envp = r->envp, .infd = -1 };
	int fd, status;

	if (n > 8192) {
		/* more than a pipe buffer holds: an unlinked temporary file */
		const char *td = getenv("TMPDIR");
		char path[4096];

		snprintf(path, sizeof(path), "%s/vccd-stdin.XXXXXX", td && *td ? td : "/tmp");
		if ((fd = mkstemp(path)) < 0)
			return 127;
		unlink(path);
		if (!vccd_fd_write_full(fd, src, n) || lseek(fd, 0, SEEK_SET)) {
			close(fd);
			return 127;
		}
	} else {
		int p[2];

		if (pipe(p))
			return 127;
		/* a probe source fits in the pipe buffer: write it all, then compile */
		if (!vccd_fd_write_full(p[1], src, n)) {
			close(p[0]);
			close(p[1]);
			return 127;
		}
		close(p[1]);
		fd = p[0];
	}
	if (vccd_backend_socket) {
		r2.fds[0] = fd;
		status = forward(&r2, vccd_backend_socket);
	} else {
		e.infd = fd;
		status = driver_job_run(r->argc, r->argv, &e);
	}
	close(fd);
	return status;
}

VPRIV int vccd_probe_cc_answer(const struct req *r, int kind)
{
	const char *out = NULL, *why;
	bool werror = false;
	int i, n;

	if (kind == VCCD_PROBE_LINK) {
		if (!vccd_capabilities_can_link) {
			say(r, "%s: error: %s\n", "cannot link user-space programs");
			return 1;
		}
	}

	for (i = 1; i < r->argc; i++)
		if (!strcmp(r->argv[i], "-Werror"))
			werror = true;
	for (i = 1; i < r->argc; i++) {
		const char *a = r->argv[i];

		if (a[0] != '-' || !a[1])
			continue;	/* the input: /dev/null or - */
		n = vccd_capabilities_cc_option_arity(a, i + 1 < r->argc ? r->argv[i + 1] : NULL, &why);
		if (!n) {
			say(r, "%s: error: unsupported option '%s'\n", why);
			return 1;
		}
		/* -D of a macro vcc predefines: -Wmacro-redefined, an error with -Werror */
		if (werror && !strncmp(a, "-D", 2) && vccd_capabilities_macro_is_predefined(a + 2)) {
			say(r, "%s: error: '%s' macro redefined\n", a + 2);
			return 1;
		}
		if (n == 2) {
			if (!strcmp(a, "-o"))
				out = r->argv[i + 1];
			i++;
		}
	}
	if (kind == VCCD_PROBE_AS || kind == VCCD_PROBE_SOURCE) {
		char src[4096];
		ssize_t len = vccd_probe_stdin_read(r, src, sizeof(src));
		int ok;

		if (len < 0) {
			say(r, "%s: error: %s\n", "probe input too large");
			return 1;
		}
		ok = kind == VCCD_PROBE_AS ? vccd_capabilities_as_source_probe(src, len) : vccd_capabilities_c_source_probe(src, len);
		if (ok < 0) {
			atomic_fetch_add(&vccd_probe_compile_count, 1);
			return vccd_probe_stdin_compile(r, src, len);
		}
		if (!ok) {
			say(r, "%s: error: %s\n", "probe source rejected");
			return 1;
		}
	}
	/* produce the (empty) output like a real compile of /dev/null would */
	if (out && strcmp(out, "/dev/null")) {
		char path[4096];
		int fd;

		if (out[0] == '/')
			snprintf(path, sizeof(path), "%s", out);
		else
			snprintf(path, sizeof(path), "%s/%s", r->cwd, out);
		fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (fd < 0) {
			say(r, "%s: error: cannot open output '%s'\n", out);
			return 1;
		}
		close(fd);
	}
	return 0;
}

/* `ld -v <opt>...`: version query plus options; answered from the table */
VPRIV bool vccd_probe_ld_is_query(const struct req *r)
{
	return r->argc >= 3 && !strcmp(r->argv[1], "-v");
}

VPRIV int vccd_probe_ld_answer(const struct req *r)
{
	const char *why;
	int i;

	for (i = 2; i < r->argc; i++)
		if (!vccd_capabilities_ld_option_is_accepted(r->argv[i], &why)) {
			say(r, "%s: error: unknown argument '%s'\n", why);
			return 1;
		}
	return 0;
}
