/* Error and warning output, with the error count and limit, in lld's wording. */

/* ---------------- diagnostics ---------------- */

static pthread_mutex_t errlock = PTHREAD_MUTEX_INITIALIZER;
static const char *argv0 = "vld";

static void diag(const char *kind, const char *fmt, va_list ap)
{
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
		VLOCK(errlock);
		if (ctx->diagnostics.last_was_multiline)
			fputc('\n', ctx->diagnostics.error_stream);
		ctx->diagnostics.last_was_multiline = strchr(m, '\n') != NULL;
		fprintf(ctx->diagnostics.error_stream, "%s: %s: %s\n", argv0, kind, m);
	}
	if (m != small)
		free(m);
}
static void error(const char *fmt, ...)
{
	int n = atomic_fetch_add(&ctx->diagnostics.error_count, 1);
	if (ctx->diagnostics.error_limit && n >= ctx->diagnostics.error_limit) {
		if (n == ctx->diagnostics.error_limit) {
			VLOCK(errlock);
			fprintf(ctx->diagnostics.error_stream, "%s: error: too many errors emitted, stopping now\n", argv0);
		}
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	diag("error", fmt, ap);
	va_end(ap);
}
static void warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	diag("warning", fmt, ap);
	va_end(ap);
}
