/* Reporting: the queue, de-duplication, counting, fatal errors and the end of a run. */
#include "../frontend/pp/pp-internal.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void diag_report_va(pp *p, uint32_t loc, const char *what, const char *fmt, va_list ap);

__attribute__((format(printf, 4, 5)))
VPRIV void diag_report(pp *p, uint32_t loc, const char *what, const char *fmt, ...)
{
	diag_report_site_pc = __builtin_return_address(0);
	va_list ap;

	va_start(ap, fmt);
	diag_report_va(p, loc, what, fmt, ap);
	va_end(ap);
}

void diag_report_extended(pp *p, const diag_info *d, const char *fmt, ...)
{
	diag_report_site_pc = __builtin_return_address(0);
	va_list ap;

	va_start(ap, fmt);
	diag_report_extended_va(p, d, fmt, ap);
	va_end(ap);
}

/* ---------------- exported for the parser ---------------- */

/* vccd's dedup: a warning in an included file, printed by another TU of this build */
static __attribute__((noinline, cold)) bool diag_sink_is_duplicate(pp *p, const diag_record *d)
{
	const pp_file_entry *e;
	const char *name;
	uint32_t nl, line, col, lc[2];
	uint64_t h = 1469598103934665603ull;

	if (d->loc == UINT32_MAX || !pp_location_is_valid(p, d->loc, &e) || e->f == p->fe[0].f ||
	    !diag_location_presumed(p, d->loc, &name, &nl, &line, &col))
		return false;
#define DH(ptr, len) do { const unsigned char *q_ = (const unsigned char *)(ptr); \
	for (size_t k_ = 0; k_ < (size_t)(len); k_++) h = (h ^ q_[k_]) * 1099511628211ull; h *= 31; } while (0)
	lc[0] = line, lc[1] = col;
	DH(e->f->path, strlen(e->f->path));
	DH(lc, sizeof lc);
	if (d->flag)
		DH(d->flag, strlen(d->flag));
	if (d->msg)
		DH(d->msg, strlen(d->msg));
#undef DH
	return p->o->dsink->seen(p->o->dsink, h);
}

void diag_record_emit(pp *p, diag_record *d)
{
	int fd = p->o->errfd ? p->o->errfd : 2;
	diag_record *top = d;
	vbuffer b = { 0 };

	if (d->level == DIAG_LEVEL_NOTE && p->diagnostic_tail)
		diag_record_note_append(top = p->diagnostic_tail, d);	/* a note reported after its diagnostic */
	else {
		if (p->diagnostic_tail)
			p->diagnostic_tail->next = d;
		else
			p->dhead = d;
		p->diagnostic_tail = d;
	}
	pp_expansion_notes_adjust(p, top, d);
	if (unlikely(p->o->dsink) && d == top && d->level == DIAG_LEVEL_WARNING && p->o->dsink->dedup && diag_sink_is_duplicate(p, d)) {
		d->dup = 1;
		p->o->dsink->ndup++;
		return;
	}
	if (top->dup)
		return;	/* a note of a left-out warning */
	if (p->o->diag_format == DIAG_FORMAT_JSON) {
		/* a record is written when it is complete: at the next one or at the end */
		if (d == top)
			diag_json_record_flush(p), p->diagnostic_pending_structured = d;
	} else if (p->o->diag_format == DIAG_FORMAT_TEXT && p->o->diag_style == DIAG_STYLE_VCC) {
		/* rendered when the next record starts (or at the end): its notes come first */
		if (d == top) {
			diag_vcc_flush(p);
			p->diagnostic_pending_text = d;
		}
	} else if (p->o->diag_format != DIAG_FORMAT_SARIF) {
		void (*r)(pp *, vbuffer *, diag_record *) = p->o->diag_style == DIAG_STYLE_NARRATED ? diag_narrated_render : diag_clang_render;

		if (!top->emitted)
			r(p, &b, top), top->emitted = 1;
		for (diag_record *k = top->kids; k; k = k->next)
			if (!k->emitted)
				r(p, &b, k), k->emitted = 1;
		(void)!write(fd, b.p, b.n);
		free(b.p);
	}
	if (d->level >= DIAG_LEVEL_ERROR)
		p->errors++;
	else if (d->level == DIAG_LEVEL_WARNING)
		p->warns++;
	if (d->level == DIAG_LEVEL_FATAL && !p->o->fatal_continue) {
		diag_finish(p);
		if (p->fatal_jump)
			longjmp(*p->fatal_jump, 1);
		exit(1);
	}
}

/* ---------------- the diag_info wrappers ---------------- */

void diag_report_extended_va(pp *p, const diag_info *di, const char *fmt, va_list ap)
{
	diag_record *d = diag_record_begin_va(p, diag_level_from_name(di->what), di->loc, fmt, ap);

	for (int i = 0; i < di->nr; i++)
		diag_record_label_add(p, d, di->r[i].b, di->r[i].e, NULL);
	for (int i = 0; i < di->nfx; i++)
		diag_record_fixit_add(p, d, di->fx[i].b, di->fx[i].e, di->fx[i].s, di->fx[i].app);
	diag_record_macro_set(d, di->ms, di->mlen);
	diag_record_emit(p, d);
}

void diag_report_va(pp *p, uint32_t loc, const char *what, const char *fmt, va_list ap)
{
	diag_record_emit(p, diag_record_begin_va(p, diag_level_from_name(what), loc, fmt, ap));
}

int diag_warning_count(const pp *p) { return p->warns; }

void diag_summary_print(pp *p)
{
	int fd = p->o->errfd ? p->o->errfd : 2;
	char b[192];
	int n = 0;

	diag_finish(p);
	if (p->o->diag_format == DIAG_FORMAT_JSON || p->o->diag_format == DIAG_FORMAT_SARIF)
		return;	/* the records say it */
	if (p->warns && p->errors)
		n = snprintf(b, sizeof(b), "%d warning%s and %d error%s generated.\n", p->warns, p->warns > 1 ? "s" : "",
			     p->errors, p->errors > 1 ? "s" : "");
	else if (p->errors)
		n = snprintf(b, sizeof(b), "%d error%s generated.\n", p->errors, p->errors > 1 ? "s" : "");
	else if (p->warns)
		n = snprintf(b, sizeof(b), "%d warning%s generated.\n", p->warns, p->warns > 1 ? "s" : "");
	if (unlikely(p->o->dsink && p->o->dsink->ndup) && n < (int)sizeof(b))
		n += snprintf(b + n, sizeof(b) - n, "%u header warning%s left out: printed by another TU of this build.\n",
			      p->o->dsink->ndup, p->o->dsink->ndup > 1 ? "s" : "");
	if (n)
		(void)!write(fd, b, (size_t)(n < (int)sizeof(b) ? n : (int)sizeof(b) - 1));
}

/* the macro id is defined now */

static void write_file(pp *p, const char *f, const vbuffer *b)
{
	int fd = p->o->errfd ? p->o->errfd : 2, out;
	char path[4096];

	if (f[0] != '/' && p->o->cwd)
		snprintf(path, sizeof(path), "%s/%s", p->o->cwd, f), f = path;
	if ((out = open(f, O_WRONLY | O_CREAT | O_TRUNC, 0644)) >= 0) {
		(void)!write(out, b->p, b->n);
		close(out);
	} else {
		dprintf(fd, "vcc: error: cannot write '%s': %s\n", f, strerror(errno));
	}
}

void diag_finish(pp *p)
{
	int fd = p->o->errfd ? p->o->errfd : 2;

	if (p->diagnostic_finished)
		return;
	p->diagnostic_finished = true;
	diag_vcc_flush(p);
	diag_json_record_flush(p);
	if (unlikely(p->o->diag_patch || p->o->fix_apply))
		diag_fixit_output(p);
	if (unlikely(p->o->dsink && p->o->dsink->record && p->dhead))
		diag_sink_records_send(p);
	if (p->o->diag_format == DIAG_FORMAT_SARIF || p->o->sarif_file) {
		vbuffer b = { 0 };

		diag_sarif_log_write(p, &b);
		if (p->o->diag_format == DIAG_FORMAT_SARIF)
			(void)!write(fd, b.p, b.n);
		if (p->o->sarif_file)
			write_file(p, p->o->sarif_file, &b);
		free(b.p);
	}
	if (unlikely(p->o->html_file != NULL)) {
		vbuffer b = { 0 };

		diag_html_page_write(p, &b);
		write_file(p, p->o->html_file, &b);
		free(b.p);
	}
}
