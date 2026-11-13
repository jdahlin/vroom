/* -Werror=unknown-warning-option: a -W option naming no warning group fails the job, as with clang. */
#include "driver-internal.h"

/* 0 to go on; 1: the job ends (status and message set) */
VPRIV int driver_warning_groups_check(struct driver_job *job)
{
	if (job->nwargs) {
		/* clang: a -W option naming no warning group is -Wunknown-warning-option, which kbuild's
		   cc-option probes make an error: gcc-only flags must fail as with clang */
		static const char *const wgroups[] = {
#include "../server/warnings.inc"
		};
		bool strict = false;

		for (int i = 0; i < job->nwargs; i++)
			if (!strcmp(job->wargs[i], "-Werror=unknown-warning-option"))
				strict = true;
		for (int i = 0; strict && i < job->nwargs; i++) {
			const char *w = job->wargs[i] + 2;
			char name[128];
			size_t n;
			bool found = false;

			if (job->wargs[i][1] != 'W' || !strcmp(w, "error") || !strcmp(w, "all") || !strcmp(w, "extra") || !strcmp(w, "everything"))
				continue;
			if (!strncmp(w, "no-", 3))
				w += 3;
			if (!strcmp(w, "error") || !strcmp(w, "everything"))
				continue;	/* (-Wno-error) */
			if (!strncmp(w, "error=", 6))
				w += 6;
			n = strcspn(w, "=");
			if (!n || n >= sizeof name)
				continue;
			memcpy(name, w, n);
			name[n] = 0;
			for (size_t k = 0; k < sizeof wgroups / sizeof *wgroups && !found; k++)
				found = !strcmp(wgroups[k], name);
			if (found && w[n] == '=' && strcmp(name, "frame-larger-than") && strcmp(name, "larger-than") &&
			    strcmp(name, "format"))
				found = false;	/* (clang takes a value for these only: -Wimplicit-fallthrough=5 is unknown) */
			if (!found) {
				dprintf(job->errfd, "error: unknown warning option '%s' [-Werror,-Wunknown-warning-option]\n", job->wargs[i]);
				job->status = 1;
				return 1;
			}
		}
	}
	return 0;
}
