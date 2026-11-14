/*
 * dispatch.c: which kind of request goes where.  vccd_request_handle() is the single entry every request passes,
 * both from a connection (worker.c) and from a recipe's tool call (recipe.c).
 *
 * The order of the tests in vccd_request_dispatch is the specification: the tool name first (sh, var/ar, nm,
 * objcopy, vld/ld), then vcc's own queries (--version, -dumpmachine, -print-file-name, --explain,
 * --fix, --vccd-diagnostics), toolchain probes answered from capabilities.c, -E, host programs, and last the compiles, which vcc takes itself and, when a
 * backend is configured, falls back to it for a TU vcc fails on.  Each branch names its kind in
 * *kind (the log and the counters use it) and returns the exit status to send back; a branch that
 * needs the client's umask relies on vccd_request_handle() setting driver_client_umask from VCC_UMASK first.  Without
 * --backend-sock nothing is forwarded: a request vcc cannot do fails, with vcc's diagnostics.  A
 * compile that succeeded gets the fixdep step (vccd_compile_fixdep_after) in vccd_request_handle().
 */
#include "vccd-internal.h"

static int vccd_request_dispatch(const struct vccd_request *r, const char **kind);

/* driver_mingw_link_run's linker: ld_main with the request's cwd and stderr */
static int vccd_mingw_link(int argc, char **argv, void *ctx)
{
	return vccd_linker_run_in_process(ctx, argc, argv);
}

VPRIV int vccd_request_handle(const struct vccd_request *r, const char **kind)
{
	int status;

	driver_client_umask = -1;	/* the shim sends VCC_UMASK; outputs then get 0666 & ~umask */
	for (char **e = r->envp; *e; e++)
		if (!strncmp(*e, "VCC_UMASK=", 10))
			driver_client_umask = (int)(strtoul(*e + 10, NULL, 8) & 0777);
	status = vccd_request_dispatch(r, kind);

	if (!strcmp(*kind, "cc-own") || !strcmp(*kind, "cc") || !strcmp(*kind, "cc-fallbk") || !strcmp(*kind, "cc-link") || !strcmp(*kind, "cc-host"))
		status = vccd_compile_fixdep_after(r, status);
	return status;
}

VPRIV int vccd_recipe_run(const struct vccd_request *r);

static int vccd_request_dispatch(const struct vccd_request *r, const char **kind)
{
	int probe;

	if (!strcmp(r->tool, "sh")) {
		/* vsh: a recipe, run here if rsh knows all of it */
		int st = vccd_recipe_run(r);

		*kind = st == SHELL_DECLINE ? "sh-decl" : "sh";
		atomic_fetch_add(st == SHELL_DECLINE ? &vccd_recipe_declined_count : &vccd_recipe_count, 1);
		return st == SHELL_DECLINE ? VCCD_STATUS_DECLINE : st;
	}
	if (!strcmp(r->tool, "vcc")) {
		const char *dg = vccd_request_environment_get(r, "VCC_DELEGATE");

		if (dg && *dg)
			return vccd_compile_delegate(r, dg, kind);
	}
	if (!strcmp(r->tool, "var") || !strcmp(r->tool, "ar") || !strcmp(r->tool, "llvm-ar")) {
		/* the archiver: thin archives written here (ar.c), never llvm-ar */
		*kind = "ar";
		atomic_fetch_add(&vccd_archive_count, 1);
		return ar_run(r->cwd, r->argc, r->argv, r->fds[1], r->fds[2]);
	}
	if (!strcmp(r->tool, "vcc-nm") || !strcmp(r->tool, "vcc-objcopy")) {
		/* ld/objcopy.c in this thread: the build's nm and objcopy, not llvm's */
		*kind = r->tool[4] == 'n' ? "nm" : "objcopy";
		atomic_fetch_add(&vccd_objcopy_count, 1);
		return vccd_objcopy_run(r, r->tool[4] == 'n');
	}
	if (!strcmp(r->tool, "vld") || !strcmp(r->tool, "ld")) {
		if (vccd_probe_ld_is_query(r)) {
			*kind = "ld-probe";
			atomic_fetch_add(&vccd_probe_count, 1);
			return vccd_probe_ld_answer(r);
		}
		if (r->argc == 2 && (!strcmp(r->argv[1], "-v") || !strcmp(r->argv[1], "--version"))) {
			*kind = "ld-version";
			atomic_fetch_add(&vccd_probe_count, 1);
			return vccd_fd_write_full(r->fds[1], vccd_capabilities_ld_version_text, strlen(vccd_capabilities_ld_version_text)) ? 0 : 1;
		}
		for (int i = 1; i < r->argc; i++)
			if (vccd_capabilities_ld_option_is_rejected(r->argv[i], i + 1 < r->argc ? r->argv[i + 1] : NULL)) {
				*kind = "ld-reject";
				atomic_fetch_add(&vccd_probe_count, 1);
				vccd_request_message_write(r, "%s: error: unsupported option '%s'\n",
				    strcmp(r->argv[i], "-z") ? r->argv[i] : r->argv[i + 1]);
				return 1;
			}
		if (vccd_linker_backend) {	/* --ld PROG */
			atomic_fetch_add(&vccd_spawn_count, 1);
			*kind = "ld-spawn";
			return vccd_program_spawn(r, vccd_linker_backend);
		}
		/* links: vld (ld/ld.c) in this thread */
		*kind = "ld";
		atomic_fetch_add(&vccd_link_count, 1);
		return vccd_linker_run(r);
	}
	if (r->argc == 2 && !strcmp(r->argv[1], "--version")) {
		*kind = "cc-version";
		atomic_fetch_add(&vccd_probe_count, 1);
		return vccd_fd_write_full(r->fds[1], vccd_capabilities_cc_version_text, strlen(vccd_capabilities_cc_version_text)) ? 0 : 1;
	}
	if (r->argc == 3 && !strcmp(r->argv[2], "-dumpmachine") && !strncmp(r->argv[1], "--target=", 9)
	    && !strchr(r->argv[1], ' ')) {
		/* the target triple normalized as clang: arch-os-env gets the vendor "unknown" */
		const char *t = r->argv[1] + 9, *d1 = strchr(t, '-'), *d2 = d1 ? strchr(d1 + 1, '-') : NULL;
		char line[512];
		int n;

		*kind = "cc-print";
		atomic_fetch_add(&vccd_probe_count, 1);
		if (d2 && !strchr(d2 + 1, '-') && !strncmp(d1 + 1, "linux", 5))
			n = snprintf(line, sizeof line, "%.*s-unknown%s\n", (int)(d1 - t), t, d1);
		else
			n = snprintf(line, sizeof line, "%s\n", t);
		return n < (int)sizeof line && vccd_fd_write_full(r->fds[1], line, n) ? 0 : 1;
	}
	if (r->argc == 2 && !strncmp(r->argv[1], "-print-file-name=", 17)) {
		const char *name = r->argv[1] + 17;
		char line[4096];
		int n;

		*kind = "cc-print";
		atomic_fetch_add(&vccd_probe_count, 1);
		n = snprintf(line, sizeof(line), "%s\n", strcmp(name, "include") ? name : vccd_capabilities_resource_include);
		return n < (int)sizeof(line) && vccd_fd_write_full(r->fds[1], line, n) ? 0 : 1;
	}
	for (int i = 1; i < r->argc; i++)
		if (!strcmp(r->argv[i], "--explain") || !strncmp(r->argv[i], "--explain=", 10) || !strcmp(r->argv[i], "--fix")) {
			/* vcc's own: the client's output and diagnostics, no clang to fall back to */
			driver_environment e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = r->fds[2], .envp = r->envp, .infd = r->fds[0] };

			*kind = r->argv[i][2] == 'e' ? "cc-explain" : "cc-fix";
			atomic_fetch_add(&vccd_compile_count, 1);
			return driver_job_run(r->argc, r->argv, &e);
		}
	if (r->argc == 2 && !strncmp(r->argv[1], "--vccd-diagnostics", 18) && (!r->argv[1][18] || r->argv[1][18] == '=')) {
		*kind = "cc-diags";
		atomic_fetch_add(&vccd_probe_count, 1);
		return vccd_diagnostics_query(r->argv[1][18] ? r->argv[1] + 19 : NULL, r->fds[1], r->fds[2]);
	}
	if (vccd_request_is_version_banner(r)) {
		/* `cc [--target=T ...] -v` with no input: the driver's banner (configure scripts read it) */
		driver_environment e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = r->fds[2], .envp = r->envp, .infd = r->fds[0] };

		*kind = "cc-version";
		atomic_fetch_add(&vccd_probe_count, 1);
		return driver_job_run(r->argc, r->argv, &e);
	}
	probe = vccd_probe_kind_classify(r);
	if (probe) {
		static const char *const names[] = { "", "cc-probe", "as-probe", "src-probe", "link-probe" };

		*kind = names[probe];
		atomic_fetch_add(&vccd_probe_count, 1);
		return vccd_probe_cc_answer(r, probe);
	}
	if (vccd_request_is_preprocess(r)) {
		driver_environment e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = r->fds[2], .envp = r->envp, .infd = r->fds[0] };

		*kind = "cc-E";
		atomic_fetch_add(&vccd_preprocess_count, 1);
		return driver_job_run(r->argc, r->argv, &e);
	}
	if (vccd_backend_socket && vccd_request_is_host_link(r)) {
		/* vcc compiles, the backend links the object with the command's options */
		const char *in = vccd_request_input_find(r), *out = NULL, *td = getenv("TMPDIR");
		char tmp[4096], **av = malloc((size_t)(r->argc + 4) * sizeof(char *)), **lv;
		bool deps = false, mt = false;
		int n = 0, ln = 0, fd, st;
		driver_environment e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = vccd_log_requests ? 2 : -1, .envp = r->envp, .infd = r->fds[0] };

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
		if (lv && driver_job_run(n, av, &e) == 0) {
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
			atomic_fetch_add(&vccd_compile_count, 1);
			atomic_fetch_add(&vccd_forward_count, 1);
			*kind = "cc-link";
			st = vccd_backend_forward_argv(r, ln, lv);
			unlink(tmp);
			free(av);
			free(lv);
			return st;
		}
		unlink(tmp);
		free(av);
		free(lv);
		atomic_fetch_add(&vccd_fallback_count, 1);
		if (vccd_log_requests)
			fprintf(stderr, "vccd fallback to clang: %s\n", in);
		*kind = "cc-fallbk";
		atomic_fetch_add(&vccd_forward_count, 1);
		return vccd_backend_forward(r, vccd_backend_socket);
	}
plain:
	if (driver_mingw_link_wanted(r->argc, r->argv)) {
		/* Windows x64: vcc compiles, vlink (ld_main, this process) links with libvccrt.a */
		driver_environment e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = r->fds[2], .envp = r->envp, .infd = r->fds[0] };
		int status = driver_mingw_link_run(r->argc, r->argv, &e, vccd_mingw_link, (void *)r);

		*kind = "cc-link";
		atomic_fetch_add(status ? &vccd_failure_count : &vccd_link_count, 1);
		return status;
	}
	if (vccd_request_is_compile(r) && !vccd_backend_socket) {
		/* no fallback: what vcc cannot compile fails, with vcc's diagnostics */
		int status = vccd_compile_in_process(r, NULL, 0, true);

		*kind = "cc-own";
		atomic_fetch_add(status ? &vccd_failure_count : &vccd_compile_count, 1);
		return status;
	}
	if (vccd_request_is_compile(r)) {
		bool in_stdin = !strcmp(vccd_request_input_find(r), "-");
		char *src = NULL;
		size_t len = 0;
		int st;

		if (in_stdin) {
			/* read here: a fallback to clang needs the same bytes */
			if (vccd_fd_read_all(r->fds[0], &src, &len)) {
				free(src);
				vccd_request_message_write(r, "%s: error: %s\n", "cannot read stdin");
				return 1;
			}
		}
		if ((st = vccd_compile_in_process(r, src, len, false)) == 0) {
			free(src);
			*kind = "cc-own";
			atomic_fetch_add(&vccd_compile_count, 1);
			return 0;
		}
		atomic_fetch_add(&vccd_fallback_count, 1);
		if (vccd_log_requests)
			fprintf(stderr, "vccd fallback to clang: %s\n", vccd_request_input_find(r));
		*kind = "cc-fallbk";
		if (in_stdin) {
			atomic_fetch_add(&vccd_forward_count, 1);
			st = vccd_probe_stdin_compile(r, src, len);
			free(src);
			return st;
		}
	} else if (!vccd_backend_socket) {
		/* the rest: host programs (no Linux target), compiled for macOS by vcc, linked by vld */
		int status = vccd_host_program_compile(r);

		*kind = "cc-host";
		atomic_fetch_add(status ? &vccd_failure_count : &vccd_host_count, 1);
		return status;
	} else {
		*kind = "cc";
		if (vccd_log_requests) {
			/* the whole command line: what is still forwarded, and why, is read from here */
			flockfile(stderr);
			fputs("vccd forward:", stderr);
			for (int i = 0; i < r->argc; i++)
				fprintf(stderr, " %s", r->argv[i]);
			fputc('\n', stderr);
			funlockfile(stderr);
		}
	}
	atomic_fetch_add(&vccd_forward_count, 1);
	return vccd_backend_forward(r, vccd_backend_socket);
}
