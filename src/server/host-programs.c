/* The host programs of a build (no --target): compile each source, then link. */
#include "vccd-internal.h"

/* options that take the next argv entry */
static bool host_with_arg(const char *a)
{
	static const char *const w[] = { "-o", "-include", "-isystem", "-iquote", "-idirafter", "-x", "-MF", "-MT",
					 "-MQ", "-I", "-D", "-U", "-L", "-arch", "-target", "-isysroot", NULL };
	for (const char *const *p = w; *p; p++)
		if (!strcmp(a, *p))
			return true;
	return false;
}

static bool is_src(const char *a)
{
	size_t n = strlen(a);
	return n > 2 && a[n - 2] == '.' && (a[n - 1] == 'c' || a[n - 1] == 'S' || a[n - 1] == 's');
}

/*
 * A host job (no --target: the build machine's own programs).  -c/-S/-E go
 * to ppdriver as they are (it compiles for the host when there is no
 * --target).  A link, with or without sources: each source is compiled to a
 * temporary object next to the output, then vld (macho-linker.c) links the
 * objects; -l maps to libSystem.  -fuse-ld= is ignored: vld is the linker.
 */
VPRIV int host_cc(const struct req *r)
{
	bool compile_only = false;
	const char *out = "a.out";
	int nsrc = 0;

#ifndef __APPLE__
	say(r, "%s: error: %s\n", "host programs need --backend-sock on this system");
	return 1;
#endif
	for (int i = 1; i < r->argc; i++) {
		const char *a = r->argv[i];
		if (!strncmp(a, "--target=", 9) || !strcmp(a, "-target")) {
			/* a target job vcc does not take (cc_is_compile, cc_is_pp): no fallback */
			say(r, "%s: error: unsupported job for target '%s'\n", a[1] == '-' ? a + 9 : "?");
			return 1;
		}
		if (!strcmp(a, "-c") || !strcmp(a, "-S") || !strcmp(a, "-E"))
			compile_only = true;
		else if (!strcmp(a, "-o") && i + 1 < r->argc)
			out = r->argv[i + 1];
		else if (!strcmp(a, "-") || (a[0] != '-' && is_src(a)))
			nsrc++;
		if (a[0] == '-' && host_with_arg(a))
			i++;
	}
	if (compile_only) {
		ppd_env e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = r->fds[2], .envp = r->envp, .infd = r->fds[0] };
		return ppd_run(r->argc, r->argv, &e);
	}

	/* compile + link: the compile options, then per source "-c SRC -o TMP" */
	char **cav = malloc(((size_t)r->argc + 8) * sizeof(char *));
	char **lav = malloc(((size_t)r->argc + 8) * sizeof(char *));
	char (*tmp)[4096] = nsrc ? malloc((size_t)nsrc * sizeof(*tmp)) : NULL;
	int status = 0, nc = 1, nl = 3, ntmp = 0;

	if (!cav || !lav || (nsrc && !tmp)) {
		free(cav);
		free(lav);
		free(tmp);
		return 1;
	}
	cav[0] = (char *)r->argv[0];
	lav[0] = "vld";
	lav[1] = "-o";
	lav[2] = (char *)out;
	for (int i = 1; i < r->argc; i++) {
		const char *a = r->argv[i];
		if (!strcmp(a, "-o") && i + 1 < r->argc) {
			i++;
			continue;
		}
		if (!strncmp(a, "-l", 2) || !strncmp(a, "-L", 2)) {
			lav[nl++] = (char *)a;
			if (!strcmp(a, "-L") && i + 1 < r->argc)
				lav[nl++] = (char *)r->argv[++i];
			continue;
		}
		if (!strncmp(a, "-fuse-ld=", 9) || !strcmp(a, "-pie") || !strcmp(a, "-no-pie"))
			continue;
		if (!strncmp(a, "-Wl,", 4)) {
			say(r, "%s: error: host link option '%s' is not supported\n", a);
			status = 1;
			goto done;
		}
		if (a[0] == '-' && a[1]) {
			cav[nc++] = (char *)a;
			if (host_with_arg(a) && i + 1 < r->argc)
				cav[nc++] = (char *)r->argv[++i];
			continue;
		}
		if (!strcmp(a, "-") || is_src(a))
			continue;	/* compiled below */
		lav[nl++] = (char *)a;	/* an object */
	}
	for (int i = 1; i < r->argc && !status; i++) {
		const char *a = r->argv[i];
		if (a[0] == '-' && a[1]) {
			if (host_with_arg(a))
				i++;
			continue;
		}
		if (strcmp(a, "-") && !is_src(a))
			continue;
		snprintf(tmp[ntmp], sizeof(tmp[ntmp]), "%s.vcc-%d.o", out, ntmp);
		int k = nc;
		cav[k++] = "-c";
		cav[k++] = (char *)a;
		cav[k++] = "-o";
		cav[k++] = tmp[ntmp];
		cav[k] = NULL;
		lav[nl++] = tmp[ntmp++];
		ppd_env e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = r->fds[2], .envp = r->envp, .infd = r->fds[0] };
		status = ppd_run(k, cav, &e);
	}
	lav[nl] = NULL;
	if (!status)
		status = ld_in_process(r, nl, lav);
	for (int t = 0; t < ntmp; t++) {
		char path[8192];
		snprintf(path, sizeof(path), "%s%s%s", tmp[t][0] == '/' ? "" : r->cwd, tmp[t][0] == '/' ? "" : "/", tmp[t]);
		unlink(path);
	}
done:
	free(cav);
	free(lav);
	free(tmp);
	return status;
}
