/* The VLD_TIME=1 phase timer: wall time, counters and page faults per phase. */

/* ---------------- timing (VLD_TIME=1) ---------------- */

static bool timing;
static double t_last;
static vperf p_last;			/* main thread's counters at the last phase */
static long flt_last;			/* process minor faults at the last phase */
static long minflt(void)
{
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	return ru.ru_minflt;
}
static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static void phase(const char *name)
{
	if (!timing)
		return;
	double t = now();
	vperf p = vperf_now(), d = vperf_sub(p, p_last);
	long f = minflt();
	fprintf(errf, "  %-28s %7.2f ms %8.1f M ins (main thread) %6ld faults\n", name, (t - t_last) * 1e3,
		d.ins / 1e6, f - flt_last);
	t_last = t;
	p_last = p;
	flt_last = f;
}
