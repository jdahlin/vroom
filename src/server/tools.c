/*
 * tools.c: run the linker, nm and objcopy inside the server instead of as processes.
 *
 * vccd_linker_run_in_process calls ld_main (ELF, or Mach-O by the first input) with the request's cwd as a
 * directory fd and a FILE on its stderr, so the link needs no fork.  vccd_linker_run adds --vroom-stubs and
 * answers -v.  nm and objcopy keep state in globals, so vccd_objcopy_run runs them one at a time
 * (under a task-aware mutex: a waiting job gives its core away).  Ownership: the fds and FILEs made
 * here are closed here on every path; the request's own fds are not (the worker closes them).
 */
#include "vccd-internal.h"


/* argv[0] plus extra, then argv[1..]: a NULL-terminated copy (free it) */
static char **vccd_argv_with_extra(const struct vccd_request *r, const char *extra)
{
	char **av = malloc(((size_t)r->argc + 2) * sizeof(char *));
	int n = 0;

	if (!av)
		return NULL;
	av[n++] = r->argv[0];
	if (extra)
		av[n++] = (char *)extra;
	for (int i = 1; i < r->argc; i++)
		av[n++] = r->argv[i];
	av[n] = NULL;
	return av;
}

/* ld_main (ELF, or Mach-O by the first input) with the request's cwd and stderr */
VPRIV int vccd_linker_run_in_process(const struct vccd_request *r, int argc, char **argv)
{
	int dfd = open(r->cwd, O_RDONLY | O_DIRECTORY | O_CLOEXEC), efd = dup(r->fds[2]), status = 1;
	FILE *err = efd >= 0 ? fdopen(efd, "w") : NULL;

	if (dfd < 0 || !err) {
		vccd_request_message_write(r, "%s: error: %s\n", "cannot set up the link");
		if (efd >= 0 && !err)
			close(efd);
	} else {
		status = ld_main(argc, argv, dfd, err);
	}
	if (err)
		fclose(err);
	if (dfd >= 0)
		close(dfd);
	return status;
}

/*
 * A link with vld in this thread.  --vroom-stubs is what vroom-ld did for
 * lld: -O0 keeps branches that -O2 proves dead, so their callees may not
 * exist; vld defines them as trapping stubs (listed in OUT.vroom-stubs).
 * A clean link has none.  -v/--version without inputs: the version line.
 */
VPRIV int vccd_linker_run(const struct vccd_request *r)
{
	bool version = false, input = false;
	char **av;
	int status;

	for (int i = 1; i < r->argc; i++) {
		const char *a = r->argv[i];

		if (!strcmp(a, "-v") || !strcmp(a, "--version") || !strcmp(a, "-V"))
			version = true;
		else if (a[0] != '-')
			input = true;
	}
	if (version && !input) {
		static const char pre[] = "vld (vroom) ";

		return vccd_fd_write_full(r->fds[1], pre, sizeof pre - 1) &&
		       vccd_fd_write_full(r->fds[1], vccd_capabilities_ld_version_text, strlen(vccd_capabilities_ld_version_text)) ? 0 : 1;
	}
	if (!(av = vccd_argv_with_extra(r, "--vroom-stubs"))) {
		vccd_request_message_write(r, "%s: error: %s\n", "out of memory");
		return 1;
	}
	status = vccd_linker_run_in_process(r, r->argc + 1, av);
	free(av);
	return status;
}

/* vcc-nm / vcc-objcopy: ld/objcopy.c keeps per-run state in globals, so one at a time */
VPRIV int vccd_objcopy_run(const struct vccd_request *r, bool nm)
{
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	int dfd = open(r->cwd, O_RDONLY | O_DIRECTORY | O_CLOEXEC), efd = dup(r->fds[2]), ofd = nm ? dup(r->fds[1]) : -1;
	FILE *err = efd >= 0 ? fdopen(efd, "w") : NULL, *out = ofd >= 0 ? fdopen(ofd, "w") : NULL;
	int status = 1;

	if (dfd < 0 || !err || (nm && !out)) {
		vccd_request_message_write(r, "%s: error: %s\n", "cannot set up nm/objcopy");
		if (efd >= 0 && !err)
			close(efd);
		if (ofd >= 0 && !out)
			close(ofd);
	} else {
		vtask_mutex_lock(&lock);
		status = nm ? nm_main(r->argc, r->argv, dfd, out, err) : objcopy_main(r->argc, r->argv, dfd, err);
		pthread_mutex_unlock(&lock);
	}
	if (out && fclose(out))
		status = status ? status : 1;
	if (err)
		fclose(err);
	if (dfd >= 0)
		close(dfd);
	return status;
}
