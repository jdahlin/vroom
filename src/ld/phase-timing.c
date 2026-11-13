/* The VLD_TIME=1 phase timer: wall time, counters and page faults per phase. */

/* ---------------- timing (VLD_TIME=1) ---------------- */

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
	if (!ctx->diagnostics.phase_timing)
		return;
	double t = now();
	vperf p = vperformance_now(), d = vperformance_subtract(p, ctx->diagnostics.phase_start_counters);
	long f = minflt();
	fprintf(ctx->diagnostics.error_stream, "  %-28s %7.2f ms %8.1f M ins (main thread) %6ld faults\n", name, (t - ctx->diagnostics.phase_start_time) * 1e3,
		d.ins / 1e6, f - ctx->diagnostics.phase_start_faults);
	ctx->diagnostics.phase_start_time = t;
	ctx->diagnostics.phase_start_counters = p;
	ctx->diagnostics.phase_start_faults = f;
}
