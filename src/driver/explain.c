/*
 * explain.c: `vcc --explain [ID]`: print a diagnostic's page, or the list of ids.
 *
 * The pages are doc/diag/*.md compiled into explain.inc by src/tools/gen-explain.py (`--check` says
 * whether it is current); the table is read only, so any thread can call explain.  driver_job_run tries it
 * first, and -1 means the command line had no --explain and the job goes on.  The id may be given as
 * -fdiagnostics-show-id prints it, `[id]`.  Output goes to env->outfd, an unknown id's error to
 * env->errfd.
 */
#include "driver-internal.h"

/* vcc --explain [ID]: the page of a diagnostic id (doc/diag, compiled in), or the
   list of ids; -1 when the command line has no --explain */
static const struct { const char *id, *text; } driver_explain_pages[] = {
#include "explain.inc"
};

VPRIV __attribute__((noinline, cold)) int explain(int argc, char **argv, const driver_environment *env)
{
	const char *id = NULL;
	bool any = false;
	int outfd = env->outfd >= 0 ? env->outfd : 1;

	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "--explain")) {
			any = true;
			id = i + 1 < argc ? argv[i + 1] : NULL;
		} else if (!strncmp(argv[i], "--explain=", 10)) {
			any = true;
			id = argv[i] + 10;
		}
	if (!any)
		return -1;
	if (id && id[0] == '[')	/* as -fdiagnostics-show-id prints it: [id] */
		id++;
	for (size_t i = 0; id && i < sizeof(driver_explain_pages) / sizeof(driver_explain_pages[0]); i++) {
		size_t l = strlen(driver_explain_pages[i].id);

		if (!strncmp(id, driver_explain_pages[i].id, l) && (!id[l] || id[l] == ']'))
			return write_all(outfd, driver_explain_pages[i].text, strlen(driver_explain_pages[i].text)) ? 1 : 0;
	}
	if (id)
		dprintf(env->errfd >= 0 ? env->errfd : 2, "vcc: error: no explanation for '%s' (vcc --explain lists the ids)\n", id);
	else
		for (size_t i = 0; i < sizeof(driver_explain_pages) / sizeof(driver_explain_pages[0]); i++)
			dprintf(outfd, "%s\n", driver_explain_pages[i].id);
	return id ? 1 : 0;
}
