/*
 * dispatch.c: which kind of request goes where.  handle() is the single entry every request passes,
 * both from a connection (worker.c) and from a recipe's tool call (recipe.c).
 *
 * The order of the tests in handle1 is the specification: the tool name first (sh, var/ar, nm,
 * objcopy, vld/ld), then vcc's own queries (--version, -dumpmachine, -print-file-name, --explain,
 * --fix, --vccd-diagnostics), toolchain probes answered from caps.c, -E, host programs, and last the compiles, which vcc takes itself and, when a
 * backend is configured, falls back to it for a TU vcc fails on.  Each branch names its kind in
 * *kind (the log and the counters use it) and returns the exit status to send back; a branch that
 * needs the client's umask relies on handle() setting ppd_umask from VCC_UMASK first.  Without
 * --backend-sock nothing is forwarded: a request vcc cannot do fails, with vcc's diagnostics.  A
 * compile that succeeded gets the fixdep step (fixdep_after) in handle().
 */
#include "vccd-internal.h"

static int handle1(const struct req *r, const char **kind);

VPRIV int handle(const struct req *r, const char **kind)
{
	int status;

	ppd_umask = -1;	/* the shim sends VCC_UMASK; outputs then get 0666 & ~umask */
	for (char **e = r->envp; *e; e++)
		if (!strncmp(*e, "VCC_UMASK=", 10))
			ppd_umask = (int)(strtoul(*e + 10, NULL, 8) & 0777);
	status = handle1(r, kind);

	if (!strcmp(*kind, "cc-own") || !strcmp(*kind, "cc") || !strcmp(*kind, "cc-fallbk") || !strcmp(*kind, "cc-link") || !strcmp(*kind, "cc-host"))
		status = fixdep_after(r, status);
	return status;
}

VPRIV int run_sh(const struct req *r);

static int handle1(const struct req *r, const char **kind)
{
	int probe;

	if (!strcmp(r->tool, "sh")) {
		/* vsh: a recipe, run here if rsh knows all of it */
		int st = run_sh(r);

		*kind = st == RSH_DECLINE ? "sh-decl" : "sh";
		atomic_fetch_add(st == RSH_DECLINE ? &n_sh_decl : &n_sh, 1);
		return st == RSH_DECLINE ? VROOM_STATUS_DECLINE : st;
	}
	if (!strcmp(r->tool, "vcc")) {
		const char *dg = req_env(r, "VCC_DELEGATE");

		if (dg && *dg)
			return delegate(r, dg, kind);
	}
	if (!strcmp(r->tool, "var") || !strcmp(r->tool, "ar") || !strcmp(r->tool, "llvm-ar")) {
		/* the archiver: thin archives written here (ar.c), never llvm-ar */
		*kind = "ar";
		atomic_fetch_add(&n_ar, 1);
		return ar_run(r->cwd, r->argc, r->argv, r->fds[1], r->fds[2]);
	}
	if (!strcmp(r->tool, "vcc-nm") || !strcmp(r->tool, "vcc-objcopy")) {
		/* ld/objcopy.c in this thread: the build's nm and objcopy, not llvm's */
		*kind = r->tool[4] == 'n' ? "nm" : "objcopy";
		atomic_fetch_add(&n_objcopy, 1);
		return objcopy_run_req(r, r->tool[4] == 'n');
	}
	if (!strcmp(r->tool, "vld") || !strcmp(r->tool, "ld")) {
		if (ld_is_probe(r)) {
			*kind = "ld-probe";
			atomic_fetch_add(&n_probe, 1);
			return ld_probe(r);
		}
		if (r->argc == 2 && (!strcmp(r->argv[1], "-v") || !strcmp(r->argv[1], "--version"))) {
			*kind = "ld-version";
			atomic_fetch_add(&n_probe, 1);
			return write_full(r->fds[1], ld_version_text, strlen(ld_version_text)) ? 0 : 1;
		}
		for (int i = 1; i < r->argc; i++)
			if (ld_rejects(r->argv[i], i + 1 < r->argc ? r->argv[i + 1] : NULL)) {
				*kind = "ld-reject";
				atomic_fetch_add(&n_probe, 1);
				say(r, "%s: error: unsupported option '%s'\n",
				    strcmp(r->argv[i], "-z") ? r->argv[i] : r->argv[i + 1]);
				return 1;
			}
		if (ld_backend) {	/* --ld PROG */
			atomic_fetch_add(&n_spawn, 1);
			*kind = "ld-spawn";
			return spawn(r, ld_backend);
		}
		/* links: vld (ld/ld.c) in this thread */
		*kind = "ld";
		atomic_fetch_add(&n_ld, 1);
		return ld_run(r);
	}
	if (r->argc == 2 && !strcmp(r->argv[1], "--version")) {
		*kind = "cc-version";
		atomic_fetch_add(&n_probe, 1);
		return write_full(r->fds[1], cc_version_text, strlen(cc_version_text)) ? 0 : 1;
	}
	if (r->argc == 3 && !strcmp(r->argv[2], "-dumpmachine") && !strncmp(r->argv[1], "--target=", 9)
	    && !strchr(r->argv[1], ' ')) {
		/* the target triple normalized as clang: arch-os-env gets the vendor "unknown" */
		const char *t = r->argv[1] + 9, *d1 = strchr(t, '-'), *d2 = d1 ? strchr(d1 + 1, '-') : NULL;
		char line[512];
		int n;

		*kind = "cc-print";
		atomic_fetch_add(&n_probe, 1);
		if (d2 && !strchr(d2 + 1, '-') && !strncmp(d1 + 1, "linux", 5))
			n = snprintf(line, sizeof line, "%.*s-unknown%s\n", (int)(d1 - t), t, d1);
		else
			n = snprintf(line, sizeof line, "%s\n", t);
		return n < (int)sizeof line && write_full(r->fds[1], line, n) ? 0 : 1;
	}
	if (r->argc == 2 && !strncmp(r->argv[1], "-print-file-name=", 17)) {
		const char *name = r->argv[1] + 17;
		char line[4096];
		int n;

		*kind = "cc-print";
		atomic_fetch_add(&n_probe, 1);
		n = snprintf(line, sizeof(line), "%s\n", strcmp(name, "include") ? name : cc_resource_include);
		return n < (int)sizeof(line) && write_full(r->fds[1], line, n) ? 0 : 1;
	}
	for (int i = 1; i < r->argc; i++)
		if (!strcmp(r->argv[i], "--explain") || !strncmp(r->argv[i], "--explain=", 10) || !strcmp(r->argv[i], "--fix")) {
			/* vcc's own: the client's output and diagnostics, no clang to fall back to */
			ppd_env e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = r->fds[2], .envp = r->envp, .infd = r->fds[0] };

			*kind = r->argv[i][2] == 'e' ? "cc-explain" : "cc-fix";
			atomic_fetch_add(&n_cc, 1);
			return ppd_run(r->argc, r->argv, &e);
		}
	if (r->argc == 2 && !strncmp(r->argv[1], "--vccd-diagnostics", 18) && (!r->argv[1][18] || r->argv[1][18] == '=')) {
		*kind = "cc-diags";
		atomic_fetch_add(&n_probe, 1);
		return dstore_query(r->argv[1][18] ? r->argv[1] + 19 : NULL, r->fds[1], r->fds[2]);
	}
	probe = cc_probe_kind(r);
	if (probe) {
		static const char *const names[] = { "", "cc-probe", "as-probe", "src-probe", "link-probe" };

		*kind = names[probe];
		atomic_fetch_add(&n_probe, 1);
		return cc_probe(r, probe);
	}
	if (cc_is_pp(r)) {
		ppd_env e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = r->fds[2], .envp = r->envp, .infd = r->fds[0] };

		*kind = "cc-E";
		atomic_fetch_add(&n_pp, 1);
		return ppd_run(r->argc, r->argv, &e);
	}
	if (backend_sock && host_cc_link(r)) {
		/* vcc compiles, the backend links the object with the command's options */
		const char *in = cc_input(r), *out = NULL, *td = getenv("TMPDIR");
		char tmp[4096], **av = malloc((size_t)(r->argc + 4) * sizeof(char *)), **lv;
		bool deps = false, mt = false;
		int n = 0, ln = 0, fd, st;
		ppd_env e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = log_requests ? 2 : -1, .envp = r->envp, .infd = r->fds[0] };

		snprintf(tmp, sizeof(tmp), "%s/vccd-%s-XXXXXX.o", td && *td ? td : "/tmp", "host");
		if (!av || (fd = mkstemps(tmp, 2)) < 0) {
			free(av);
			goto plain;
		}
		close(fd);
		lv = malloc((size_t)(r->argc + 1) * sizeof(char *));
		for (int i = 0; i < r->argc; i++) {
			const char *a = r->argv[i];

			if (!strcmp(a, "-o") && i + 1 < r->argc)
				out = r->argv[i + 1];
			deps |= !strncmp(a, "-Wp,-MMD,", 9) || !strncmp(a, "-Wp,-MD,", 8) || !strcmp(a, "-MD") || !strcmp(a, "-MMD");
			mt |= !strcmp(a, "-MT") || !strcmp(a, "-MQ");
		}
		/* the compile: -c into the temporary; the dependency target stays the program, as clang's */
		for (int i = 0; i < r->argc; i++) {
			if (!strcmp(r->argv[i], "-o") && i + 1 < r->argc) {
				av[n++] = r->argv[i++];
				av[n++] = tmp;
				continue;
			}
			av[n++] = r->argv[i];
		}
		av[n++] = "-c";
		if (deps && !mt)
			av[n++] = "-MT", av[n++] = (char *)out;
		av[n] = NULL;
		if (lv && ppd_run(n, av, &e) == 0) {
			/* the link: the object for the source, no dependency options */
			for (int i = 0; i < r->argc; i++) {
				const char *a = r->argv[i];

				if (!strncmp(a, "-Wp,-M", 6) || !strcmp(a, "-MD") || !strcmp(a, "-MMD") || !strcmp(a, "-MP"))
					continue;
				if ((!strcmp(a, "-MF") || !strcmp(a, "-MT") || !strcmp(a, "-MQ")) && i + 1 < r->argc) {
					i++;
					continue;
				}
				lv[ln++] = a == in ? tmp : r->argv[i];
			}
			lv[ln] = NULL;
			atomic_fetch_add(&n_cc, 1);
			atomic_fetch_add(&n_fwd, 1);
			*kind = "cc-link";
			st = forward_argv(r, ln, lv);
			unlink(tmp);
			free(av);
			free(lv);
			return st;
		}
		unlink(tmp);
		free(av);
		free(lv);
		atomic_fetch_add(&n_fallback, 1);
		if (log_requests)
			fprintf(stderr, "vccd fallback to clang: %s\n", in);
		*kind = "cc-fallbk";
		atomic_fetch_add(&n_fwd, 1);
		return forward(r, backend_sock);
	}
plain:
	if (cc_is_compile(r) && !backend_sock) {
		/* no fallback: what vcc cannot compile fails, with vcc's diagnostics */
		int status = cc_own(r, NULL, 0, true);

		*kind = "cc-own";
		atomic_fetch_add(status ? &n_fail : &n_cc, 1);
		return status;
	}
	if (cc_is_compile(r)) {
		bool in_stdin = !strcmp(cc_input(r), "-");
		char *src = NULL;
		size_t len = 0;
		int st;

		if (in_stdin) {
			/* read here: a fallback to clang needs the same bytes */
			if (read_all(r->fds[0], &src, &len)) {
				free(src);
				say(r, "%s: error: %s\n", "cannot read stdin");
				return 1;
			}
		}
		if ((st = cc_own(r, src, len, false)) == 0) {
			free(src);
			*kind = "cc-own";
			atomic_fetch_add(&n_cc, 1);
			return 0;
		}
		atomic_fetch_add(&n_fallback, 1);
		if (log_requests)
			fprintf(stderr, "vccd fallback to clang: %s\n", cc_input(r));
		*kind = "cc-fallbk";
		if (in_stdin) {
			atomic_fetch_add(&n_fwd, 1);
			st = compile_stdin(r, src, len);
			free(src);
			return st;
		}
	} else if (!backend_sock) {
		/* the rest: host programs (no Linux target), compiled for macOS by vcc, linked by vld */
		int status = host_cc(r);

		*kind = "cc-host";
		atomic_fetch_add(status ? &n_fail : &n_host, 1);
		return status;
	} else {
		*kind = "cc";
		if (log_requests) {
			/* the whole command line: what is still forwarded, and why, is read from here */
			flockfile(stderr);
			fputs("vccd forward:", stderr);
			for (int i = 0; i < r->argc; i++)
				fprintf(stderr, " %s", r->argv[i]);
			fputc('\n', stderr);
			funlockfile(stderr);
		}
	}
	atomic_fetch_add(&n_fwd, 1);
	return forward(r, backend_sock);
}
