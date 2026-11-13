/* Error and warning output, with the error count and limit, in lld's wording. */

/* ---------------- diagnostics ---------------- */

static FILE *errf;
static atomic_int nerrors;
static int error_limit = 20;
static pthread_mutex_t ld_diagnostics_lock = PTHREAD_MUTEX_INITIALIZER;
static const char *argv0 = "vld";

static void ld_diagnostic_print(const char *kind, const char *fmt, va_list ap)
{
	static bool prev_multi;	/* lld: a blank line after a multi-line diagnostic */
	va_list aq;
	char small[512], *m = small;
	int n;

	va_copy(aq, ap);
	n = vsnprintf(small, sizeof small, fmt, ap);
	if (n >= (int)sizeof small && (m = malloc((size_t)n + 1)))
		vsnprintf(m, (size_t)n + 1, fmt, aq);
	va_end(aq);
	if (!m)
		m = small;
	{
		VLOCK(ld_diagnostics_lock);
		if (prev_multi)
			fputc('\n', errf);
		prev_multi = strchr(m, '\n') != NULL;
		fprintf(errf, "%s: %s: %s\n", argv0, kind, m);
	}
	if (m != small)
		free(m);
}
static void ld_error_report(const char *fmt, ...)
{
	int n = atomic_fetch_add(&nerrors, 1);
	if (error_limit && n >= error_limit) {
		if (n == error_limit) {
			VLOCK(ld_diagnostics_lock);
			fprintf(errf, "%s: error: too many errors emitted, stopping now\n", argv0);
		}
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	ld_diagnostic_print("error", fmt, ap);
	va_end(ap);
}
static void ld_warning_report(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ld_diagnostic_print("warning", fmt, ap);
	va_end(ap);
}
