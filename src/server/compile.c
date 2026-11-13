/*
 * compile.c: run a -c job in this thread (vccd_compile_in_process), delegate to another program when vcc fails
 * (delegate), and finish kbuild's fixdep step (vccd_compile_fixdep_after).
 *
 * vccd_compile_in_process gives the driver (driver_job_run) a scratch file for diagnostics instead of the client's stderr:
 * a TU vcc fails on is passed on to clang or PROG, which reports in its own words, so vcc's messages
 * reach the client only when the job succeeded or when there is no fallback (`final`).  The
 * diagnostics store (diagnostics-store.c) gets the job's records in either case and drops them again if the TU
 * fell back.  The capture file is per thread and reused.  delegate is the VCC_DELEGATE mode for
 * builds that are not the kernel: a plain -c of a .c file is compiled here, everything else runs as
 * PROG.  vccd_compile_fixdep_after is called by vccd_request_handle() for the compile kinds only, and only when the request
 * carries the VCC_FIXDEP_* variables.
 */
#include "vccd-internal.h"

/* a -c job vcc compiles itself.  Its diagnostics go to a scratch file and to the
   client only if it succeeds: a TU vcc fails on goes to clang (or PROG), which
   reports.  final: there is no fallback (no backend), so a failure's
   diagnostics go to the client too.  Its records replace the TU's in the
   build's store (diagnostics-store.c).  src[0..len): the input `-` when vccd has read it
   already (NULL: fd 0). */
static _Thread_local int vccd_compile_capture_fd = -1;

VPRIV int vccd_compile_in_process(const struct vccd_request *r, const char *src, size_t len, bool final)
{
	const char *dd = vccd_request_environment_get(r, "VCC_DIAG_DEDUP");
	vccd_diagnostics_job j;
	driver_environment e = { .cwd = r->cwd, .outfd = r->fds[1], .errfd = -1, .envp = r->envp, .dsink = &j.sink,
		      .errtty = isatty(r->fds[2]), .infd = r->fds[0], .in_src = src, .in_len = len };
	int st, to;
	off_t n;

	if (vccd_compile_capture_fd < 0) {
		FILE *f = tmpfile();	/* already unlinked */

		vccd_compile_capture_fd = f ? fcntl(fileno(f), F_DUPFD_CLOEXEC, 3) : -1;
		if (f)
			fclose(f);
	}
	e.errfd = vccd_compile_capture_fd >= 0 ? vccd_compile_capture_fd : vccd_log_requests ? 2 : -1;
	vccd_diagnostics_job_begin(&j, r->cwd, r->argv[r->argc - 1], dd && *dd && strcmp(dd, "0"));
	st = driver_job_run(r->argc, r->argv, &e);
	vccd_diagnostics_job_end(&j, st == 0 || final);
	if (vccd_compile_capture_fd >= 0 && (n = lseek(vccd_compile_capture_fd, 0, SEEK_CUR)) > 0) {
		char buf[1 << 14];

		to = st == 0 || final ? r->fds[2] : vccd_log_requests ? 2 : -1;
		for (off_t o = 0; to >= 0 && o < n;) {
			ssize_t k = pread(vccd_compile_capture_fd, buf, (size_t)(n - o < (off_t)sizeof buf ? n - o : (off_t)sizeof buf), o);

			if (k <= 0 || !vccd_fd_write_full(to, buf, (size_t)k))
				break;
			o += k;
		}
		if (ftruncate(vccd_compile_capture_fd, 0) || lseek(vccd_compile_capture_fd, 0, SEEK_SET))
			close(vccd_compile_capture_fd), vccd_compile_capture_fd = -1;
	}
	return st;
}

VPRIV int vccd_compile_delegate(const struct vccd_request *r, const char *prog, const char **kind)
{
	if (vccd_request_is_delegated_compile(r, vccd_request_environment_get(r, "VCC_DELEGATE_SKIP"))) {
		if (vccd_compile_in_process(r, NULL, 0, false) == 0) {
			*kind = "cc-own";
			atomic_fetch_add(&vccd_compile_count, 1);
			return 0;
		}
		atomic_fetch_add(&vccd_fallback_count, 1);
		if (vccd_log_requests)
			fprintf(stderr, "vccd fallback to %s: %s\n", prog, r->argv[r->argc - 1]);
		*kind = "dg-fallbk";
	} else {
		*kind = "dg";
	}
	atomic_fetch_add(&vccd_spawn_count, 1);
	return vccd_program_spawn(r, prog);
}
/*
 * Kbuild's cmd_and_fixdep done here (patches/0008): a compile whose request
 * carries VCC_FIXDEP_D/T/O/CMD (dep file, target, .cmd file, saved command
 * line) gets fixdep's .cmd written and its dep file removed once it has
 * succeeded, so the recipe runs no fixdep and no rm.
 */
VPRIV int vccd_compile_fixdep_after(const struct vccd_request *r, int status)
{
	const char *d = vccd_request_environment_get(r, "VCC_FIXDEP_D"), *t, *o, *c;

	if (status || !d)
		return status;
	t = vccd_request_environment_get(r, "VCC_FIXDEP_T");
	o = vccd_request_environment_get(r, "VCC_FIXDEP_O");
	c = vccd_request_environment_get(r, "VCC_FIXDEP_CMD");
	if (!t || !o || !c) {
		vccd_request_message_write(r, "vccd: %s without VCC_FIXDEP_T/O/CMD\n", "VCC_FIXDEP_D");
		return 2;
	}
	return fixdep_run(r->cwd, d, t, c, o, r->fds[2]);
}
