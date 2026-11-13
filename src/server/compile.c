/*
 * compile.c: run a -c job in this thread (cc_own), delegate to another program when vcc fails
 * (delegate), and finish kbuild's fixdep step (fixdep_after).
 *
 * cc_own gives the driver (ppd_run) a scratch file for diagnostics instead of the client's stderr:
 * a TU vcc fails on is passed on to clang or PROG, which reports in its own words, so vcc's messages
 * reach the client only when the job succeeded or when there is no fallback (`final`).  The
 * diagnostics store (dstore.c) gets the job's records in either case and drops them again if the TU
 * fell back.  The capture file is per thread and reused.  delegate is the VCC_DELEGATE mode for
 * builds that are not the kernel: a plain -c of a .c file is compiled here, everything else runs as
 * PROG.  fixdep_after is called by handle() for the compile kinds only, and only when the request
 * carries the VCC_FIXDEP_* variables.
 */
#include "vccd-internal.h"

/* a -c job vcc compiles itself.  Its diagnostics go to a scratch file and to the
   client only if it succeeds: a TU vcc fails on goes to clang (or PROG), which
   reports.  final: there is no fallback (no backend), so a failure's
   diagnostics go to the client too.  Its records replace the TU's in the
   build's store (dstore.c).  src[0..len): the input `-` when vccd has read it
   already (NULL: fd 0). */
static _Thread_local int cap_fd = -1;

VPRIV int cc_own(const struct req *r, const char *src, size_t len, bool final)
{
	const char *dd = req_env(r, "VCC_DIAG_DEDUP");
	dstore_job j;
	ppd_env e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = -1, .envp = r->envp, .dsink = &j.sink,
		      .errtty = isatty(r->fds[2]), .infd = r->fds[0], .in_src = src, .in_len = len };
	int st, to;
	off_t n;

	if (cap_fd < 0) {
		FILE *f = tmpfile();	/* already unlinked */

		cap_fd = f ? fcntl(fileno(f), F_DUPFD_CLOEXEC, 3) : -1;
		if (f)
			fclose(f);
	}
	e.errfd = cap_fd >= 0 ? cap_fd : log_requests ? 2 : -1;
	dstore_begin(&j, r->cwd, r->argv[r->argc - 1], dd && *dd && strcmp(dd, "0"));
	st = ppd_run(r->argc, r->argv, &e);
	dstore_end(&j, st == 0 || final);
	if (cap_fd >= 0 && (n = lseek(cap_fd, 0, SEEK_CUR)) > 0) {
		char buf[1 << 14];

		to = st == 0 || final ? r->fds[2] : log_requests ? 2 : -1;
		for (off_t o = 0; to >= 0 && o < n;) {
			ssize_t k = pread(cap_fd, buf, (size_t)(n - o < (off_t)sizeof buf ? n - o : (off_t)sizeof buf), o);

			if (k <= 0 || !write_full(to, buf, (size_t)k))
				break;
			o += k;
		}
		if (ftruncate(cap_fd, 0) || lseek(cap_fd, 0, SEEK_SET))
			close(cap_fd), cap_fd = -1;
	}
	return st;
}

VPRIV int delegate(const struct req *r, const char *prog, const char **kind)
{
	if (dg_is_compile(r, req_env(r, "VCC_DELEGATE_SKIP"))) {
		if (cc_own(r, NULL, 0, false) == 0) {
			*kind = "cc-own";
			atomic_fetch_add(&n_cc, 1);
			return 0;
		}
		atomic_fetch_add(&n_fallback, 1);
		if (log_requests)
			fprintf(stderr, "vccd fallback to %s: %s\n", prog, r->argv[r->argc - 1]);
		*kind = "dg-fallbk";
	} else {
		*kind = "dg";
	}
	atomic_fetch_add(&n_spawn, 1);
	return spawn(r, prog);
}
/*
 * Kbuild's cmd_and_fixdep done here (patches/0008): a compile whose request
 * carries VCC_FIXDEP_D/T/O/CMD (dep file, target, .cmd file, saved command
 * line) gets fixdep's .cmd written and its dep file removed once it has
 * succeeded, so the recipe runs no fixdep and no rm.
 */
VPRIV int fixdep_after(const struct req *r, int status)
{
	const char *d = req_env(r, "VCC_FIXDEP_D"), *t, *o, *c;

	if (status || !d)
		return status;
	t = req_env(r, "VCC_FIXDEP_T");
	o = req_env(r, "VCC_FIXDEP_O");
	c = req_env(r, "VCC_FIXDEP_CMD");
	if (!t || !o || !c) {
		say(r, "vccd: %s without VCC_FIXDEP_T/O/CMD\n", "VCC_FIXDEP_D");
		return 2;
	}
	return vfixdep(r->cwd, d, t, c, o, r->fds[2]);
}
