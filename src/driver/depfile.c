/* The make rule of -MD/-M (clang's DependencyFileGenerator) and the depfile's name and target. */
#include "driver-internal.h"

/* make-style dependency file, as clang -MD writes it */
/* the make rule, as clang's DependencyFileGenerator: lines wrapped at 75
 * columns, -MMD/-MM without system headers, -MP phony targets.
 * depfile NULL: to -o or stdout (-M/-MM). */
VPRIV int write_deps(const pp *p, const char *cwd, const char *depfile, const char *target, int nosys, int phony,
		      const char *first, const char *out, int outfd, int errfd)
{
	static _Thread_local const char **names;
	static _Thread_local size_t cap;
	size_t n, col;

	/* grown until every file fits (a full table may have been cut short) */
	while (!names || (n = pp_files(p, names, cap, nosys)) == cap) {
		free(names);
		cap = cap ? 2 * cap : 4096;
		names = malloc(cap * sizeof(*names));
	}
	buf d = { .r = &bufr[3] };
	int r;

	bstr(&d, target);
	bstr(&d, ":");
	col = strlen(target) + 1;
	for (size_t i = first ? (size_t)-1 : 0; i + 1 < n + 1; i++) {	/* first: Darwin's SDKSettings.json */
		const char *f = i == (size_t)-1 ? first : names[i];
		size_t l;

		if (i == 0 && !strcmp(f, "<stdin>"))
			continue;	/* clang: a main file from stdin is no dependency */
		while (f[0] == '.' && f[1] == '/')	/* clang drops "./" */
			f += 2;
		l = strlen(f);
		if (col + l + 3 > 75) {	/* room for a trailing " \\" */
			bstr(&d, " \\\n ");
			col = 2;
		}
		bstr(&d, " ");
		bstr(&d, f);
		col += l + 1;
	}
	bstr(&d, "\n");
	if (phony)
		for (size_t i = first ? (size_t)-1 : 1; i + 1 < n + 1; i += i == (size_t)-1 ? 2 : 1) {	/* not names[0], the main file */
			const char *f = i == (size_t)-1 ? first : names[i];

			while (f[0] == '.' && f[1] == '/')
				f += 2;
			bstr(&d, f);
			bstr(&d, ":\n");
		}
	r = depfile ? write_file(cwd, depfile, d.p, d.n, errfd) : write_out(cwd, out, outfd, d.p, d.n, errfd);
	bfree(&d);
	return r;
}

/* the dependency file's target and name */
VPRIV void driver_depfile_names(struct driver_job *job)
{
	/* dependency rules, as clang: the target is -o (not for -M/-MM) or the
	   source's basename with .o; -MD's file is the target's with .d */
	job->dtarget = job->deptarget;
	job->dnosys = job->mdeps ? job->mdeps == 2 : job->md == 2;
	job->dfirst = NULL;

	if (!job->dtarget)
		job->dtarget = job->out && strcmp(job->out, "-") && !job->mdeps ? job->out : obj_name(job->main_file, job->dtbuf, sizeof(job->dtbuf));
	if (job->md && !job->mdeps && !job->depfile) {
		const char *st = job->out && strcmp(job->out, "-") ? job->out : obj_name(job->main_file, job->dtbuf + 2048, 2048);
		const char *sl = strrchr(st, '/'), *dot = strrchr(st, '.');

		if (dot && sl && dot < sl)
			dot = NULL;
		snprintf(job->dfbuf, sizeof(job->dfbuf), "%.*s.d", (int)(dot ? dot - st : (long)strlen(st)), st);
		job->depfile = job->dfbuf;
	}
}
