/*
 * depfile.c: the make rule of -MD/-MMD/-M/-MM, and the name and target it is written under.
 *
 * driver_depfile_write reproduces clang's DependencyFileGenerator byte for byte (bench/vcc-diff.py deps checks
 * it): names from pp_file_names wrapped at 75 columns, `./` dropped, `<stdin>` left out, -MMD/-MM
 * without system headers, -MP phony targets for every file but the main one.  `first` is a file that
 * comes before the rest (Darwin's SDKSettings.json, which clang reads).  driver_depfile_names sets
 * the target (-MT, -o, or the source's object name) and the file's name (-MF, or the target's with
 * .d) in the job.  The text is built in a job buffer and written with driver_output_file_write/driver_output_write; a failure
 * is reported on the job's error fd and returned as 1.
 */
#include "driver-internal.h"

/* make-style dependency file, as clang -MD writes it */
/* the make rule, as clang's DependencyFileGenerator: lines wrapped at 75
 * columns, -MMD/-MM without system headers, -MP phony targets.
 * depfile NULL: to -o or stdout (-M/-MM). */
VPRIV int driver_depfile_write(const pp *p, const char *cwd, const char *depfile, const char *target, int nosys, int phony,
		      const char *first, const char *out, int outfd, int errfd)
{
	static _Thread_local const char **names;
	static _Thread_local size_t cap;
	size_t n, col;

	/* grown until every file fits (a full table may have been cut short) */
	while (!names || (n = pp_file_names(p, names, cap, nosys)) == cap) {
		free(names);
		cap = cap ? 2 * cap : 4096;
		names = malloc(cap * sizeof(*names));
	}
	driver_buffer d = { .region = &driver_buffer_region[3] };
	int r;

	driver_buffer_append_string(&d, target);
	driver_buffer_append_string(&d, ":");
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
			driver_buffer_append_string(&d, " \\\n ");
			col = 2;
		}
		driver_buffer_append_string(&d, " ");
		driver_buffer_append_string(&d, f);
		col += l + 1;
	}
	driver_buffer_append_string(&d, "\n");
	if (phony)
		for (size_t i = first ? (size_t)-1 : 1; i + 1 < n + 1; i += i == (size_t)-1 ? 2 : 1) {	/* not names[0], the main file */
			const char *f = i == (size_t)-1 ? first : names[i];

			while (f[0] == '.' && f[1] == '/')
				f += 2;
			driver_buffer_append_string(&d, f);
			driver_buffer_append_string(&d, ":\n");
		}
	r = depfile ? driver_output_file_write(cwd, depfile, d.data, d.length, errfd) : driver_output_write(cwd, out, outfd, d.data, d.length, errfd);
	driver_buffer_free(&d);
	return r;
}

/* the dependency file's target and name */
VPRIV void driver_depfile_names(struct driver_job *job)
{
	/* dependency rules, as clang: the target is -o (not for -M/-MM) or the
	   source's basename with .o; -MD's file is the target's with .d */
	job->depfile_target = job->dependency_target;
	job->depfile_omit_system = job->dependency_print_mode ? job->dependency_print_mode == 2 : job->dependency_file_mode == 2;
	job->depfile_first_input = NULL;

	if (!job->depfile_target)
		job->depfile_target = job->output_file && strcmp(job->output_file, "-") && !job->dependency_print_mode ? job->output_file : driver_object_name(job->main_file, job->dependency_target_buffer, sizeof(job->dependency_target_buffer));
	if (job->dependency_file_mode && !job->dependency_print_mode && !job->depfile) {
		const char *st = job->output_file && strcmp(job->output_file, "-") ? job->output_file : driver_object_name(job->main_file, job->dependency_target_buffer + 2048, 2048);
		const char *sl = strrchr(st, '/'), *dot = strrchr(st, '.');

		if (dot && sl && dot < sl)
			dot = NULL;
		snprintf(job->depfile_name_buffer, sizeof(job->depfile_name_buffer), "%.*s.d", (int)(dot ? dot - st : (long)strlen(st)), st);
		job->depfile = job->depfile_name_buffer;
	}
}
