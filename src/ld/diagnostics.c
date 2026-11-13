/* Error and warning output, with the error count and limit, in lld's wording. */

#include "ld-internal.h"

/* ---------------- diagnostics ---------------- */

static pthread_mutex_t ld_diagnostics_lock = PTHREAD_MUTEX_INITIALIZER;
static const char *argv0 = "vld";

static void ld_diagnostic_print(struct ld_context *ctx, const char *kind, const char *fmt, va_list ap)
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
		VLOCK(ld_diagnostics_lock);
		if (ctx->diagnostics.last_was_multiline)
			fputc('\n', ctx->diagnostics.error_stream);
		ctx->diagnostics.last_was_multiline = strchr(m, '\n') != NULL;
		fprintf(ctx->diagnostics.error_stream, "%s: %s: %s\n", argv0, kind, m);
	}
	if (m != small)
		free(m);
}
VPRIV void ld_error_report(struct ld_context *ctx, const char *fmt, ...)
{
	int n = atomic_fetch_add(&ctx->diagnostics.error_count, 1);
	if (ctx->diagnostics.error_limit && n >= ctx->diagnostics.error_limit) {
		if (n == ctx->diagnostics.error_limit) {
			VLOCK(ld_diagnostics_lock);
			fprintf(ctx->diagnostics.error_stream, "%s: error: too many errors emitted, stopping now\n", argv0);
		}
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	ld_diagnostic_print(ctx, "error", fmt, ap);
	va_end(ap);
}
VPRIV void ld_warning_report(struct ld_context *ctx, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ld_diagnostic_print(ctx, "warning", fmt, ap);
	va_end(ap);
}
