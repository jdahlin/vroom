/* Reporting: the queue, de-duplication, counting, fatal errors and the end of a run. */
#include "../frontend/pp/ppimpl.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void pp_vdiag(pp *p, uint32_t loc, const char *what, const char *fmt, va_list ap);

__attribute__((format(printf, 4, 5)))
VPRIV void pp_diag(pp *p, uint32_t loc, const char *what, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	va_list ap;

	va_start(ap, fmt);
	pp_vdiag(p, loc, what, fmt, ap);
	va_end(ap);
}

void pp_diagx(pp *p, const pp_dinfo *d, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	va_list ap;

	va_start(ap, fmt);
	pp_vdiagx(p, d, fmt, ap);
	va_end(ap);
}

/* ---------------- exported for the parser ---------------- */

/* vccd's dedup: a warning in an included file, printed by another TU of this build */
static __attribute__((noinline, cold)) bool dsink_dup(pp *p, const vdiag *d)
{
	const fent *e;
	const char *name;
	uint32_t nl, line, col, lc[2];
	uint64_t h = 1469598103934665603ull;

	if (d->loc == UINT32_MAX || !loc_ok(p, d->loc, &e) || e->f == p->fe[0].f ||
	    !diag_where(p, d->loc, &name, &nl, &line, &col))
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

void pp_diag_emit(pp *p, vdiag *d)
{
	int fd = p->o->errfd ? p->o->errfd : 2;
	vdiag *top = d;
	vbuffer b = { 0 };

	if (d->level == VDL_NOTE && p->dtail)
		kid_add(top = p->dtail, d);	/* a note reported after its diagnostic */
	else {
		if (p->dtail)
			p->dtail->next = d;
		else
			p->dhead = d;
		p->dtail = d;
	}
	macro_adjust(p, top, d);
	if (unlikely(p->o->dsink) && d == top && d->level == VDL_WARNING && p->o->dsink->dedup && dsink_dup(p, d)) {
		d->dup = 1;
		p->o->dsink->ndup++;
		return;
	}
	if (top->dup)
		return;	/* a note of a left-out warning */
	if (p->o->diag_format == PP_DIAG_JSON) {
		/* a record is written when it is complete: at the next one or at the end */
		if (d == top)
			diag_json_flush(p), p->dpend = d;
	} else if (p->o->diag_format == PP_DIAG_TEXT && p->o->diag_style == PP_DSTYLE_VCC) {
		/* rendered when the next record starts (or at the end): its notes come first */
		if (d == top) {
			diag_text_flush(p);
			p->tpend = d;
		}
	} else if (p->o->diag_format != PP_DIAG_SARIF) {
		void (*r)(pp *, vbuffer *, vdiag *) = p->o->diag_style == PP_DSTYLE_NARRATED ? diag_narrate : render_clang;

		if (!top->emitted)
			r(p, &b, top), top->emitted = 1;
		for (vdiag *k = top->kids; k; k = k->next)
			if (!k->emitted)
				r(p, &b, k), k->emitted = 1;
		(void)!write(fd, b.p, b.n);
		free(b.p);
	}
	if (d->level >= VDL_ERROR)
		p->errors++;
	else if (d->level == VDL_WARNING)
		p->warns++;
	if (d->level == VDL_FATAL && !p->o->fatal_continue) {
		pp_diag_finish(p);
		if (p->fatal_jb)
			longjmp(*p->fatal_jb, 1);
		exit(1);
	}
}

/* ---------------- the pp_dinfo wrappers ---------------- */

void pp_vdiagx(pp *p, const pp_dinfo *di, const char *fmt, va_list ap)
{
	vdiag *d = pp_diag_vbegin(p, level_of(di->what), di->loc, fmt, ap);

	for (int i = 0; i < di->nr; i++)
		pp_diag_label(p, d, di->r[i].b, di->r[i].e, NULL);
	for (int i = 0; i < di->nfx; i++)
		pp_diag_fix(p, d, di->fx[i].b, di->fx[i].e, di->fx[i].s, di->fx[i].app);
	pp_diag_macro(d, di->ms, di->mlen);
	pp_diag_emit(p, d);
}

void pp_vdiag(pp *p, uint32_t loc, const char *what, const char *fmt, va_list ap)
{
	pp_diag_emit(p, pp_diag_vbegin(p, level_of(what), loc, fmt, ap));
}

int pp_warnings(const pp *p) { return p->warns; }

void pp_diag_summary(pp *p)
{
	int fd = p->o->errfd ? p->o->errfd : 2;
	char b[192];
	int n = 0;

	pp_diag_finish(p);
	if (p->o->diag_format == PP_DIAG_JSON || p->o->diag_format == PP_DIAG_SARIF)
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

void pp_diag_finish(pp *p)
{
	int fd = p->o->errfd ? p->o->errfd : 2;

	if (p->dfinished)
		return;
	p->dfinished = true;
	diag_text_flush(p);
	diag_json_flush(p);
	if (unlikely(p->o->diag_patch || p->o->fix_apply))
		diag_fixits_out(p);
	if (unlikely(p->o->dsink && p->o->dsink->record && p->dhead))
		dsink_records(p);
	if (p->o->diag_format == PP_DIAG_SARIF || p->o->sarif_file) {
		vbuffer b = { 0 };

		sarif_log(p, &b);
		if (p->o->diag_format == PP_DIAG_SARIF)
			(void)!write(fd, b.p, b.n);
		if (p->o->sarif_file)
			write_file(p, p->o->sarif_file, &b);
		free(b.p);
	}
	if (unlikely(p->o->html_file != NULL)) {
		vbuffer b = { 0 };

		html_page(p, &b);
		write_file(p, p->o->html_file, &b);
		free(b.p);
	}
}
