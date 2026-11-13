/* vcc --explain: the page of a diagnostic id, compiled in. */
#include "driver-internal.h"

/* vcc --explain [ID]: the page of a diagnostic id (doc/diag, compiled in), or the
   list of ids; -1 when the command line has no --explain */
static const struct { const char *id, *text; } explain_tab[] = {
#include "explain.inc"
};

VPRIV __attribute__((noinline, cold)) int explain(int argc, char **argv, const ppd_env *env)
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
	for (size_t i = 0; id && i < sizeof(explain_tab) / sizeof(explain_tab[0]); i++) {
		size_t l = strlen(explain_tab[i].id);

		if (!strncmp(id, explain_tab[i].id, l) && (!id[l] || id[l] == ']'))
			return write_all(outfd, explain_tab[i].text, strlen(explain_tab[i].text)) ? 1 : 0;
	}
	if (id)
		dprintf(env->errfd >= 0 ? env->errfd : 2, "vcc: error: no explanation for '%s' (vcc --explain lists the ids)\n", id);
	else
		for (size_t i = 0; i < sizeof(explain_tab) / sizeof(explain_tab[0]); i++)
			dprintf(outfd, "%s\n", explain_tab[i].id);
	return id ? 1 : 0;
}
