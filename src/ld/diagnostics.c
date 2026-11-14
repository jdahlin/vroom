/*
 * Error and warning output of the linker tools, with the error count and
 * limit, in lld's wording: the one diagnostics function of vld's ELF and
 * Mach-O links, vobjcopy and vnm (struct ld_diagnostics in ld/ld.h; each run
 * has its own), and the ELF linker's two wrappers on its context.
 *
 * This file is part of ld.c's unity TU, which is compiled twice (ld.o for
 * vlink, ldlib.o for vccd); the public functions are defined once in each
 * binary because a binary links only one of the two.
 */

#include "ld-internal.h"

/* one line at a time from all threads and all links of the process */
static pthread_mutex_t ld_diagnostics_lock = PTHREAD_MUTEX_INITIALIZER;

static void ld_diagnostic_print(struct ld_diagnostics *d, const char *kind, const char *fmt, va_list ap)
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
		if (d->blank_line_after_multiline) {
			if (d->last_was_multiline)
				fputc('\n', d->error_stream);
			d->last_was_multiline = strchr(m, '\n') != NULL;
		}
		fprintf(d->error_stream, "%s: %s: %s\n", d->program, kind, m);
	}
	if (m != small)
		free(m);
}

/* kind "error" counts against the limit; past it nothing is printed */
void ld_diagnostic_report(struct ld_diagnostics *d, const char *kind, const char *fmt, va_list ap)
{
	if (!strcmp(kind, "error")) {
		int n = atomic_fetch_add(&d->error_count, 1);
		if (d->error_limit && n >= d->error_limit) {
			if (n == d->error_limit && d->limit_is_announced) {
				VLOCK(ld_diagnostics_lock);
				fprintf(d->error_stream, "%s: error: too many errors emitted, stopping now\n", d->program);
			}
			return;
		}
	}
	ld_diagnostic_print(d, kind, fmt, ap);
}

void ld_diagnostic_error(struct ld_diagnostics *d, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ld_diagnostic_report(d, "error", fmt, ap);
	va_end(ap);
}

void ld_diagnostic_warning(struct ld_diagnostics *d, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ld_diagnostic_report(d, "warning", fmt, ap);
	va_end(ap);
}

VPRIV void ld_error_report(struct ld_context *ctx, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ld_diagnostic_report(&ctx->diagnostics, "error", fmt, ap);
	va_end(ap);
}

VPRIV void ld_warning_report(struct ld_context *ctx, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ld_diagnostic_report(&ctx->diagnostics, "warning", fmt, ap);
	va_end(ap);
}
