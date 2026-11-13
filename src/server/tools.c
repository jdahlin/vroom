/* ld, nm and objcopy run in the server's own process. */
#include "vccd-internal.h"


/* argv[0] plus extra, then argv[1..]: a NULL-terminated copy (free it) */
static char **argv_with(const struct req *r, const char *extra)
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
VPRIV int ld_in_process(const struct req *r, int argc, char **argv)
{
	int dfd = open(r->cwd, O_RDONLY | O_DIRECTORY | O_CLOEXEC), efd = dup(r->fds[2]), status = 1;
	FILE *err = efd >= 0 ? fdopen(efd, "w") : NULL;

	if (dfd < 0 || !err) {
		say(r, "%s: error: %s\n", "cannot set up the link");
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
VPRIV int ld_run(const struct req *r)
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

		return write_full(r->fds[1], pre, sizeof pre - 1) &&
		       write_full(r->fds[1], ld_version_text, strlen(ld_version_text)) ? 0 : 1;
	}
	if (!(av = argv_with(r, "--vroom-stubs"))) {
		say(r, "%s: error: %s\n", "out of memory");
		return 1;
	}
	status = ld_in_process(r, r->argc + 1, av);
	free(av);
	return status;
}

/* vcc-nm / vcc-objcopy: ld/objcopy.c keeps per-run state in globals, so one at a time */
VPRIV int objcopy_run_req(const struct req *r, bool nm)
{
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	int dfd = open(r->cwd, O_RDONLY | O_DIRECTORY | O_CLOEXEC), efd = dup(r->fds[2]), ofd = nm ? dup(r->fds[1]) : -1;
	FILE *err = efd >= 0 ? fdopen(efd, "w") : NULL, *out = ofd >= 0 ? fdopen(ofd, "w") : NULL;
	int status = 1;

	if (dfd < 0 || !err || (nm && !out)) {
		say(r, "%s: error: %s\n", "cannot set up nm/objcopy");
		if (efd >= 0 && !err)
			close(efd);
		if (ofd >= 0 && !out)
			close(ofd);
	} else {
		pthread_mutex_lock(&lock);
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
