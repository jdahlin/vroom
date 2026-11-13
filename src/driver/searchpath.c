/*
 * searchpath.c: the include search path of a job, in clang's order: -iquote, -I, CPATH, -isystem,
 * C_INCLUDE_PATH, the target's system headers, -idirafter.
 *
 * driver_searchpath_alloc sizes one per-thread array for the longest command line seen (each list
 * holds at most argc entries); the option loop fills the lists and driver_searchpath_build merges
 * them into job->search_directories, dropping exact duplicates from -I on and recording the boundaries the
 * preprocessor needs (po.nquote, nsys1, the extern "C" range xc0..xc1 that shows as flags in line
 * markers).  The order is observable in `-H`, `-M` and in which header is found, and is checked
 * against clang by vcc-diff.py hdr and deps: change it only against clang.  vcc's own headers
 * (VCC_INCDIR) go before clang's resource directory, so arm_neon.h and stdatomic.h are ours.  On
 * Darwin this also finds SDKSettings.json, which clang lists first among the dependencies.
 */
#include "driver-internal.h"

/* CPATH-style lists (kept out of driver_job_run: its size decides how clang
   compiles the argument loop; vbench pp +3.7 % instructions inline) */
static __attribute__((noinline)) size_t driver_searchpath_environment_count(const char *v)
{
	size_t k = v && *v ? 1 : 0;

	for (; v && *v; v++)
		k += *v == ':';
	return k;
}

/* the elements of v appended to all[n..] (not repeating one from nquote on); an
   empty element is the current directory.  slot: which per-thread copy holds them */
static __attribute__((noinline)) int driver_searchpath_environment_split(const char *v, const char **all, int n, int nquote, int slot)
{
	static _Thread_local char *copy[2];
	char *s, *t;

	if (!v || !*v)
		return n;
	free(copy[slot]);
	copy[slot] = s = strdup(v);
	for (; s; s = t) {
		const char *d;
		int dup = 0;

		if ((t = strchr(s, ':')))
			*t++ = 0;
		d = *s ? s : ".";
		for (int j = nquote; j < n; j++)
			dup |= !strcmp(all[j], d);
		if (!dup)
			all[n++] = d;
	}
	return n;
}

/* each list holds at most argc entries, all of them plus the defaults: one
   per-thread allocation, grown to the longest command line seen */
VPRIV void driver_searchpath_alloc(struct driver_job *job, int argc, const driver_environment *env)
{
	static _Thread_local const char **dirv;
	static _Thread_local size_t dircap;
	job->cpath_value = driver_environment_get(env, "CPATH"), job->c_include_path_value = driver_environment_get(env, "C_INCLUDE_PATH");
	size_t nenvd = driver_searchpath_environment_count(job->cpath_value) + driver_searchpath_environment_count(job->c_include_path_value);
	size_t per = (size_t)argc + 64 + nenvd;

	if (5 * per > dircap) {
		free(dirv);
		dircap = 5 * per;
		dirv = malloc(dircap * sizeof(*dirv));
	}
	job->quote_directories = dirv, job->include_directories = dirv + per, job->system_directories = dirv + 2 * per, job->after_directories = dirv + 3 * per,
	job->search_directories = dirv + 4 * per;
}

VPRIV void driver_searchpath_build(struct driver_job *job, const driver_environment *env)
{
	size_t nsys, nfwdirs;
	const char *const *sysdirs_darwin = driver_predef_darwin_system_directories(&nsys);
	const char *const *fwdirs_darwin = driver_predef_darwin_framework_directories(&nfwdirs);

	/* search path: -iquote, -I, -isystem, -idirafter; exact duplicates dropped */
	for (int k = 0; k < job->quote_count; k++) job->search_directories[job->search_count++] = job->quote_directories[k];
	job->pp_options.nquote = job->search_count;
#define ADD(d) do { int dup_ = 0; for (int j = job->pp_options.nquote; j < job->search_count; j++) dup_ |= !strcmp(job->search_directories[j], d); if (!dup_) job->search_directories[job->search_count++] = d; } while (0)
	for (int k = 0; k < job->include_count; k++) ADD(job->include_directories[k]);
	job->search_count = driver_searchpath_environment_split(job->cpath_value, job->search_directories, job->search_count, job->pp_options.nquote, 0);	/* CPATH: after -I, as clang */
	job->pp_options.nsys1 = job->search_count + 1;
	for (int k = 0; k < job->system_count; k++) {
		/* vcc's own headers (arm_neon.h) go before the clang resource directory */
		if (strstr(job->system_directories[k], "/lib/clang/") && strlen(job->system_directories[k]) > 8 &&
		    !strcmp(job->system_directories[k] + strlen(job->system_directories[k]) - 8, "/include"))
			ADD(VCC_INCDIR);
		ADD(job->system_directories[k]);
	}
	job->search_count = driver_searchpath_environment_split(job->c_include_path_value, job->search_directories, job->search_count, job->pp_options.nquote, 1);	/* C_INCLUDE_PATH: after -isystem */
	/* Darwin: vcc's own <stdatomic.h>, <arm_neon.h>, <arm_acle.h> ahead of the SDK's */
	if (job->darwin && !job->no_standard_includes)
		ADD(VCC_INCDIR);
	if (job->darwin && !job->no_standard_includes)
		for (size_t k = 0; k < nsys; k++) {
			size_t l = strlen(sysdirs_darwin[k]);
			int xc = l > 12 && !strcmp(sysdirs_darwin[k] + l - 12, "/usr/include");

			if (xc)
				job->pp_options.xc0 = job->search_count;
			ADD(sysdirs_darwin[k]);
			if (xc)
				job->pp_options.xc1 = job->search_count;
		}
	if (job->darwin && (job->dependency_print_mode || job->depfile || job->show_includes)) {
		/* clang reads the SDK's SDKSettings.json (the default sysroot: the SDK
		   of the system directories), and it is the first dependency */
		static __thread char sdk[1024];

		for (size_t k = 0; k < nsys && !job->depfile_first_input; k++) {
			const char *d = sysdirs_darwin[k], *u = strstr(d, ".sdk/usr/include");

			if (u && snprintf(sdk, sizeof(sdk), "%.*s/SDKSettings.json", (int)(u + 4 - d), d) < (int)sizeof(sdk) &&
			    !access(sdk, F_OK))
				job->depfile_first_input = sdk;
		}
		if (job->depfile_first_input && job->show_includes)
			dprintf(job->errfd, ". %s\n", job->depfile_first_input);	/* -H: clang's first line too */
		if (!job->dependency_print_mode && !job->depfile)
			job->depfile_first_input = NULL;
	}
	(void)job->hosted;
#ifdef VCC_RESDIR
	/* hosted Linux: clang's resource headers, then glibc's (multiarch first) */
	if (job->hosted && !job->darwin && !job->no_standard_includes) {
		ADD(VCC_RESDIR);
		ADD("/usr/local/include");
		job->pp_options.xc0 = job->search_count;	/* clang: extern "C" system directories (" 3 4" in line markers) */
		ADD(job->target_x64 ? "/usr/include/x86_64-linux-gnu" : "/usr/include/aarch64-linux-gnu");
		ADD("/usr/include");
		job->pp_options.xc1 = job->search_count;
	}
#endif
	for (int k = 0; k < job->after_count; k++) ADD(job->after_directories[k]);
	if (job->darwin && !job->no_standard_includes) {
		job->pp_options.fwdirs = fwdirs_darwin;
		job->pp_options.nfwdirs = (int)(nfwdirs);
	}
	job->pp_options.dirs = job->search_directories;
	job->pp_options.ndirs = job->search_count;
	job->pp_options.edirs = job->embed_directories, job->pp_options.nedirs = job->embed_count;
#undef ADD
}
