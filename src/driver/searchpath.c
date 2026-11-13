/*
 * searchpath.c: the include search path of a job, in clang's order: -iquote, -I, CPATH, -isystem,
 * C_INCLUDE_PATH, the target's system headers, -idirafter.
 *
 * driver_searchpath_alloc sizes one per-thread array for the longest command line seen (each list
 * holds at most argc entries); the option loop fills the lists and driver_searchpath_build merges
 * them into job->all, dropping exact duplicates from -I on and recording the boundaries the
 * preprocessor needs (po.nquote, nsys1, the extern "C" range xc0..xc1 that shows as flags in line
 * markers).  The order is observable in `-H`, `-M` and in which header is found, and is checked
 * against clang by vcc-diff.py hdr and deps: change it only against clang.  vcc's own headers
 * (VCC_INCDIR) go before clang's resource directory, so arm_neon.h and stdatomic.h are ours.  On
 * Darwin this also finds SDKSettings.json, which clang lists first among the dependencies.
 */
#include "driver-internal.h"

/* CPATH-style lists (kept out of ppd_run: its size decides how clang
   compiles the argument loop; vbench pp +3.7 % instructions inline) */
static __attribute__((noinline)) size_t env_ndirs(const char *v)
{
	size_t k = v && *v ? 1 : 0;

	for (; v && *v; v++)
		k += *v == ':';
	return k;
}

/* the elements of v appended to all[n..] (not repeating one from nquote on); an
   empty element is the current directory.  slot: which per-thread copy holds them */
static __attribute__((noinline)) int env_dirs(const char *v, const char **all, int n, int nquote, int slot)
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
VPRIV void driver_searchpath_alloc(struct driver_job *job, int argc, const ppd_env *env)
{
	static _Thread_local const char **dirv;
	static _Thread_local size_t dircap;
	job->cpath = ppd_getenv(env, "CPATH"), job->cipath = ppd_getenv(env, "C_INCLUDE_PATH");
	size_t nenvd = env_ndirs(job->cpath) + env_ndirs(job->cipath);
	size_t per = (size_t)argc + 64 + nenvd;

	if (5 * per > dircap) {
		free(dirv);
		dircap = 5 * per;
		dirv = malloc(dircap * sizeof(*dirv));
	}
	job->dirs_q = dirv, job->dirs_i = dirv + per, job->dirs_s = dirv + 2 * per, job->dirs_a = dirv + 3 * per,
	job->all = dirv + 4 * per;
}

VPRIV void driver_searchpath_build(struct driver_job *job, const ppd_env *env)
{
	size_t nsys, nfwdirs;
	const char *const *sysdirs_darwin = driver_predef_darwin_sysdirs(&nsys);
	const char *const *fwdirs_darwin = driver_predef_darwin_fwdirs(&nfwdirs);

	/* search path: -iquote, -I, -isystem, -idirafter; exact duplicates dropped */
	for (int k = 0; k < job->nq; k++) job->all[job->n++] = job->dirs_q[k];
	job->po.nquote = job->n;
#define ADD(d) do { int dup_ = 0; for (int j = job->po.nquote; j < job->n; j++) dup_ |= !strcmp(job->all[j], d); if (!dup_) job->all[job->n++] = d; } while (0)
	for (int k = 0; k < job->ni; k++) ADD(job->dirs_i[k]);
	job->n = env_dirs(job->cpath, job->all, job->n, job->po.nquote, 0);	/* CPATH: after -I, as clang */
	job->po.nsys1 = job->n + 1;
	for (int k = 0; k < job->ns; k++) {
		/* vcc's own headers (arm_neon.h) go before the clang resource directory */
		if (strstr(job->dirs_s[k], "/lib/clang/") && strlen(job->dirs_s[k]) > 8 &&
		    !strcmp(job->dirs_s[k] + strlen(job->dirs_s[k]) - 8, "/include"))
			ADD(VCC_INCDIR);
		ADD(job->dirs_s[k]);
	}
	job->n = env_dirs(job->cipath, job->all, job->n, job->po.nquote, 1);	/* C_INCLUDE_PATH: after -isystem */
	/* Darwin: vcc's own <stdatomic.h>, <arm_neon.h>, <arm_acle.h> ahead of the SDK's */
	if (job->darwin && !job->nostdinc)
		ADD(VCC_INCDIR);
	if (job->darwin && !job->nostdinc)
		for (size_t k = 0; k < nsys; k++) {
			size_t l = strlen(sysdirs_darwin[k]);
			int xc = l > 12 && !strcmp(sysdirs_darwin[k] + l - 12, "/usr/include");

			if (xc)
				job->po.xc0 = job->n;
			ADD(sysdirs_darwin[k]);
			if (xc)
				job->po.xc1 = job->n;
		}
	if (job->darwin && (job->mdeps || job->depfile || job->showinc)) {
		/* clang reads the SDK's SDKSettings.json (the default sysroot: the SDK
		   of the system directories), and it is the first dependency */
		static __thread char sdk[1024];

		for (size_t k = 0; k < nsys && !job->dfirst; k++) {
			const char *d = sysdirs_darwin[k], *u = strstr(d, ".sdk/usr/include");

			if (u && snprintf(sdk, sizeof(sdk), "%.*s/SDKSettings.json", (int)(u + 4 - d), d) < (int)sizeof(sdk) &&
			    !access(sdk, F_OK))
				job->dfirst = sdk;
		}
		if (job->dfirst && job->showinc)
			dprintf(job->errfd, ". %s\n", job->dfirst);	/* -H: clang's first line too */
		if (!job->mdeps && !job->depfile)
			job->dfirst = NULL;
	}
	(void)job->hosted;
#ifdef VCC_RESDIR
	/* hosted Linux: clang's resource headers, then glibc's (multiarch first) */
	if (job->hosted && !job->darwin && !job->nostdinc) {
		ADD(VCC_RESDIR);
		ADD("/usr/local/include");
		job->po.xc0 = job->n;	/* clang: extern "C" system directories (" 3 4" in line markers) */
		ADD(job->x86 ? "/usr/include/x86_64-linux-gnu" : "/usr/include/aarch64-linux-gnu");
		ADD("/usr/include");
		job->po.xc1 = job->n;
	}
#endif
	for (int k = 0; k < job->na; k++) ADD(job->dirs_a[k]);
	if (job->darwin && !job->nostdinc) {
		job->po.fwdirs = fwdirs_darwin;
		job->po.nfwdirs = (int)(nfwdirs);
	}
	job->po.dirs = job->all;
	job->po.ndirs = job->n;
	job->po.edirs = job->dirs_e, job->po.nedirs = job->ne;
#undef ADD
}
