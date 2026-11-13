/* The VLD_TIME=1 phase timer: wall time, counters and page faults per phase. */

/* ---------------- timing (VLD_TIME=1) ---------------- */

static bool timing;
static double t_last;
static vperf p_last;			/* main thread's counters at the last phase */
static long flt_last;			/* process minor faults at the last phase */
static long ld_minor_fault_count(void)
{
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	return ru.ru_minflt;
}
static double ld_clock_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static void ld_phase_time_report(const char *name)
{
	if (!timing)
		return;
	double t = ld_clock_now();
	vperf p = vperformance_now(), d = vperformance_subtract(p, p_last);
	long f = ld_minor_fault_count();
	fprintf(errf, "  %-28s %7.2f ms %8.1f M ins (main thread) %6ld faults\n", name, (t - t_last) * 1e3,
		d.ins / 1e6, f - flt_last);
	t_last = t;
	p_last = p;
	flt_last = f;
}
