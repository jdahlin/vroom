/* Toolchain probes: jobs answered from the capability table without compiling. */
#include "vccd-internal.h"

/*
 * Probes: jobs whose answer vcc's capability table gives without compiling.
 *
 *   PROBE_CC    -c/-S of C from /dev/null (cc-option, Kconfig's cc-probe):
 *               the answer depends on the options alone;
 *   PROBE_AS    -c of assembler-with-cpp from stdin into /dev/null or a
 *               try-run temporary (as-instr),
 *   PROBE_SRC   -c/-S of C from stdin into the same (`echo ... | $(CC)`):
 *               the source is looked up, and compiled for real (forwarded
 *               with the bytes already read) if it is not in the table;
 *   PROBE_LINK  a link of C from stdin into /dev/null (cc-can-link.sh).
 *
 * Anything else (a real file, stdin into a real output) is a real job.
 */

static bool probe_output(const char *out)
{
	return out && (!strcmp(out, "/dev/null") || strstr(out, ".tmp_"));
}

VPRIV int cc_probe_kind(const struct req *r)
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
			return PROBE_NONE;
		if (!strcmp(a, "--param") || !strcmp(a, "-mllvm")) {
			i++;	/* its value is not an input; cc_probe rejects the option */
			continue;
		}
		if (!strcmp(a, "-")) {
			if (in_stdin)
				return PROBE_NONE;
			in_stdin = true;
			continue;
		}
		if (!strcmp(a, "--param") && i + 1 < r->argc) {
			i++;	/* its value is no input (cc_probe rejects --param, as clang: unused) */
			continue;
		}
		if (a[0] != '-') {
			if (strcmp(a, "/dev/null"))
				return PROBE_NONE;
			devnull = true;
			continue;
		}
		n = cc_option(a, i + 1 < r->argc ? r->argv[i + 1] : NULL, &why);
		if (n == 2) {
			if (!strcmp(a, "-x"))
				lang = r->argv[i + 1];
			if (!strcmp(a, "-o"))
				out = r->argv[i + 1];
			i++;
		}
	}
	if (devnull && !in_stdin && compile && !strcmp(lang, "c"))
		return PROBE_CC;
	if (in_stdin && !devnull && compile && !strcmp(lang, "assembler-with-cpp") &&
	    probe_output(out))
		return PROBE_AS;
	if (in_stdin && !devnull && compile && !strcmp(lang, "c") && probe_output(out))
		return PROBE_SRC;
	if (in_stdin && !devnull && !compile && !strcmp(lang, "c") && out &&
	    !strcmp(out, "/dev/null"))
		return PROBE_LINK;
	return PROBE_NONE;
}

/* the probe's stdin, up to sizeof(buf); -1 if it is larger or unreadable */
static ssize_t read_stdin(const struct req *r, char *buf, size_t size)
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
VPRIV int read_all(int fd, char **buf, size_t *len)
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
VPRIV int compile_stdin(const struct req *r, const char *src, size_t n)
{
	struct req r2 = *r;
	ppd_env e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = r->fds[2], .envp = r->envp, .infd = -1 };
	int fd, status;

	if (n > 8192) {
		/* more than a pipe buffer holds: an unlinked temporary file */
		const char *td = getenv("TMPDIR");
		char path[4096];

		snprintf(path, sizeof(path), "%s/vccd-stdin.XXXXXX", td && *td ? td : "/tmp");
		if ((fd = mkstemp(path)) < 0)
			return 127;
		unlink(path);
		if (!write_full(fd, src, n) || lseek(fd, 0, SEEK_SET)) {
			close(fd);
			return 127;
		}
	} else {
		int p[2];

		if (pipe(p))
			return 127;
		/* a probe source fits in the pipe buffer: write it all, then compile */
		if (!write_full(p[1], src, n)) {
			close(p[0]);
			close(p[1]);
			return 127;
		}
		close(p[1]);
		fd = p[0];
	}
	if (backend_sock) {
		r2.fds[0] = fd;
		status = forward(&r2, backend_sock);
	} else {
		e.infd = fd;
		status = ppd_run(r->argc, r->argv, &e);
	}
	close(fd);
	return status;
}

VPRIV int cc_probe(const struct req *r, int kind)
{
	const char *out = NULL, *why;
	bool werror = false;
	int i, n;

	if (kind == PROBE_LINK) {
		if (!cc_can_link) {
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
		n = cc_option(a, i + 1 < r->argc ? r->argv[i + 1] : NULL, &why);
		if (!n) {
			say(r, "%s: error: unsupported option '%s'\n", why);
			return 1;
		}
		/* -D of a macro vcc predefines: -Wmacro-redefined, an error with -Werror */
		if (werror && !strncmp(a, "-D", 2) && cc_predefined(a + 2)) {
			say(r, "%s: error: '%s' macro redefined\n", a + 2);
			return 1;
		}
		if (n == 2) {
			if (!strcmp(a, "-o"))
				out = r->argv[i + 1];
			i++;
		}
	}
	if (kind == PROBE_AS || kind == PROBE_SRC) {
		char src[4096];
		ssize_t len = read_stdin(r, src, sizeof(src));
		int ok;

		if (len < 0) {
			say(r, "%s: error: %s\n", "probe input too large");
			return 1;
		}
		ok = kind == PROBE_AS ? as_source_probe(src, len) : c_source_probe(src, len);
		if (ok < 0) {
			atomic_fetch_add(&n_probe_cc, 1);
			return compile_stdin(r, src, len);
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
VPRIV bool ld_is_probe(const struct req *r)
{
	return r->argc >= 3 && !strcmp(r->argv[1], "-v");
}

VPRIV int ld_probe(const struct req *r)
{
	const char *why;
	int i;

	for (i = 2; i < r->argc; i++)
		if (!ld_option(r->argv[i], &why)) {
			say(r, "%s: error: unknown argument '%s'\n", why);
			return 1;
		}
	return 0;
}
