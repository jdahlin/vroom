/* The diagnostic record: ids, level names and the builder that fills it. */
#include "../frontend/pp/ppimpl.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------------- records ---------------- */

/* vcc's formats and their ids (src/tools/gen-diagids.py) */
static const struct dfmt { const char *fmt, *id; } dfmts[] = {
#define DID(n, cls, grp)
#define DFMT(f, n) { f, #n },
#include "diagnostic-ids.inc"
#undef DID
#undef DFMT
};

/* the id of a format (NULL: not in the table) */
static const char *fmt_id(const char *fmt)
{
	size_t lo = 0, hi = sizeof(dfmts) / sizeof(dfmts[0]);

	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int r = strcmp(dfmts[mid].fmt, fmt);

		if (!r)
			return dfmts[mid].id;
		if (r < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return NULL;
}

/* the id of format fmt (for a caller that formats the message itself) */
const char *pp_diag_fmt_id(const char *fmt)
{
	return fmt_id(fmt);
}

_Thread_local const void *pp_diag_pc;
_Thread_local const char *pp_diag_next_id;	/* the next record's id when its format serves several (then NULL) */

static const char *const lvname[] = { "note", "remark", "warning", "error", "fatal error" };

const char *pp_diag_level(int level) { return lvname[level]; }

VPRIV int level_of(const char *what)
{
	switch (what[0]) {
	case 'e': return VDL_ERROR;
	case 'f': return VDL_FATAL;
	case 'w': return VDL_WARNING;
	case 'r': return VDL_REMARK;
	default: return VDL_NOTE;
	}
}

static char *dstr(pp *p, const char *s, size_t n)
{
	char *r = varena_alloc(&p->perm, n + 1);

	memcpy(r, s, n);
	r[n] = 0;
	return r;
}

/* 'v4\x01(vector of 4 'int' values)' -> 'v4' (vector of 4 'int' values) (cc's type names) */
static void vec_suffix(char *m, size_t cap)
{
	char *x;

	while ((x = strchr(m, '\x01'))) {
		char *q = x + 1;
		int dep = 0;

		for (; *q; q++)
			if (*q == '(')
				dep++;
			else if (*q == ')' && !--dep)
				break;
		if (!*q || q[1] != '\'') {
			*x = ' ';	/* (not inside quotes: just a space) */
			continue;
		}
		/* x: "\x01(...)'" -> "' (...)" : the same length */
		memmove(x + 2, x + 1, (size_t)(q - x));	/* "(...)" one to the right */
		x[0] = '\'', x[1] = ' ';
		(void)cap;
	}
}

vdiag *pp_diag_vbegin(pp *p, int level, uint32_t loc, const char *fmt, va_list ap)
{
	vdiag *d = varena_alloc(&p->perm, sizeof(*d));
	char msg[2048];
	int n = vsnprintf(msg, sizeof(msg), fmt, ap);

	if (unlikely(n > 0 && memchr(msg, '\x01', (size_t)n < sizeof(msg) ? (size_t)n : sizeof(msg) - 1)))
		vec_suffix(msg, sizeof(msg));	/* (the same length) */
	memset(d, 0, sizeof(*d));
	d->level = (uint8_t)level;
	d->loc = loc;
	d->fmt = fmt;
	d->id = pp_diag_next_id ? pp_diag_next_id : fmt_id(fmt);
	pp_diag_next_id = NULL;
	d->track = pp_diag_pc;
	pp_diag_pc = NULL;
	d->msg = dstr(p, msg, n < 0 ? 0 : (size_t)n < sizeof(msg) ? (size_t)n : sizeof(msg) - 1);
	return d;
}

vdiag *pp_diag_begin(pp *p, int level, uint32_t loc, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	va_list ap;
	vdiag *d;

	va_start(ap, fmt);
	d = pp_diag_vbegin(p, level, loc, fmt, ap);
	va_end(ap);
	return d;
}

void pp_diag_label(pp *p, vdiag *d, uint32_t b, uint32_t e, const char *label)
{
	if (d->nspan == d->cspan) {
		uint32_t c = d->cspan ? d->cspan * 2 : 4;
		vdspan *n = varena_alloc(&p->perm, c * sizeof(*n));

		if (d->nspan)
			memcpy(n, d->span, d->nspan * sizeof(*n));
		d->span = n, d->cspan = c;
	}
	d->span[d->nspan++] = (vdspan){ b, e, label ? dstr(p, label, strlen(label)) : NULL };
}

int pp_diag_art_on(const pp *p)
{
	return p->o->diag_style != PP_DSTYLE_VCC ? 0 : p->o->diag_unicode ? 2 : 1;
}

void pp_diag_art(pp *p, vdiag *d, const char *s, size_t n)
{
	d->art = dstr(p, s, n);
}

void pp_diag_help(pp *p, vdiag *d, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n >= 0)
		d->help = dstr(p, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

/* [b, e) lies in a macro's name in the source: the text to change is in the macro's body,
   which a fix-it at the invocation would not reach (clang drops such fix-its); a range
   that covers the whole invocation and more ("&& MASK") is a fix of the source */

void pp_diag_fix(pp *p, vdiag *d, uint32_t b, uint32_t e, const char *s, int app)
{
	if (unlikely(!(app & VDA_NAME) && fix_in_macro(p, b, e)))
		return;
	app &= ~VDA_NAME;
	if (d->nfix == d->cfix) {
		uint32_t c = d->cfix ? d->cfix * 2 : 4;
		vdfix *n = varena_alloc(&p->perm, c * sizeof(*n));

		if (d->nfix)
			memcpy(n, d->fix, d->nfix * sizeof(*n));
		d->fix = n, d->cfix = c;
	}
	d->fix[d->nfix++] = (vdfix){ b, e, dstr(p, s, strlen(s)), (uint8_t)app };
}

void pp_diag_flag(vdiag *d, const char *flag)
{
	d->flag = flag;
}

void pp_diag_macro(vdiag *d, const char *ms, uint32_t mlen)
{
	d->ms = ms, d->mlen = mlen;
}

VPRIV void kid_add(vdiag *d, vdiag *k)
{
	if (d->klast)
		d->klast->next = k;
	else
		d->kids = k;
	d->klast = k;
}

/* n among top's notes right after after (top itself: first) */
VPRIV void kid_after(vdiag *top, vdiag *after, vdiag *n)
{
	if (after == top) {
		n->next = top->kids, top->kids = n;
		if (!top->klast)
			top->klast = n;
		return;
	}
	n->next = after->next, after->next = n;
	if (top->klast == after)
		top->klast = n;
}

vdiag *pp_diag_note(pp *p, vdiag *d, uint32_t loc, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	va_list ap;
	vdiag *k;

	va_start(ap, fmt);
	k = pp_diag_vbegin(p, VDL_NOTE, loc, fmt, ap);
	va_end(ap);
	kid_add(d, k);
	return k;
}

const vdiag *pp_diag_records(const pp *p) { return p->dhead; }

/* clang: a token from a macro expansion has the caret at the invocation and
   a range over all of it, and a note at the macro's body */
