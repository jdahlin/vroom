/* vcc front end: diagnostics in clang's format */
#include "ccimpl.h"

/* ---------------- diagnostics ---------------- */

/* clang's -ferror-limit (default 20, 0: none): stop the TU with its fatal error */
VPRIV void error_limit(cc *c)
{
	int lim = pp_error_limit(c->p);

	if (!lim)
		lim = 20;
	if (lim > 0 && pp_errors(c->p) + 1 >= lim) {
		pp_dinfo d = { "fatal error", UINT32_MAX, NULL, 0, NULL, 0, NULL, 0 };

		pp_diagx(c->p, &d, "too many errors emitted, stopping now [-ferror-limit=]");
		longjmp(c->bail, 1);
	}
}

VPRIV void diag(cc *c, uint32_t loc, const char *what, const char *fmt, va_list ap)
{
	c->ndiag++;
	pp_vdiag(c->p, loc, what, fmt, ap);
}

__attribute__((format(printf, 3, 4)))
VPRIV void cc_error(cc *c, uint32_t loc, const char *fmt, ...)
{
	va_list ap;

	error_limit(c);
	va_start(ap, fmt);
	diag(c, loc, "error", fmt, ap);
	va_end(ap);
	c->errors++;
}

__attribute__((format(printf, 3, 4), noreturn))
VPRIV void fatal(cc *c, uint32_t loc, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	diag(c, loc, "error", fmt, ap);
	va_end(ap);
	c->errors++;
	longjmp(c->bail, 1);
}

/* a diagnostic with ranges and fix-its (clang's rendering, pp.c) */
__attribute__((format(printf, 8, 9)))
VPRIV void report(cc *c, const char *what, uint32_t loc, const pp_span *r, int nr,
		   const pp_fixit *fx, int nfx, const char *fmt, ...)
{
	pp_dinfo d = { what, loc, r, nr, fx, nfx, NULL, 0 };
	va_list ap;

	if (!strcmp(what, "error"))
		error_limit(c), c->errors++;
	if (strcmp(what, "note"))
		c->ndiag++;
	va_start(ap, fmt);
	pp_vdiagx(c->p, &d, fmt, ap);
	va_end(ap);
}

__attribute__((format(printf, 3, 4)))
VPRIV void note(cc *c, uint32_t loc, const char *fmt, ...)
{
	pp_dinfo d = { "note", loc, NULL, 0, NULL, 0, NULL, 0 };
	va_list ap;

	va_start(ap, fmt);
	pp_vdiagx(c->p, &d, fmt, ap);
	va_end(ap);
}

const char *const w_all[] = { "all", "most", NULL };
const char *const w_conv[] = { "conversion", NULL };

/* a warning of clang's flag (on: on by default); false if it is off */
__attribute__((format(printf, 10, 11)))
VPRIV bool cc_warnx(cc *c, const char *flag, bool on, const char *const *groups, uint32_t loc,
		  const pp_span *r, int nr, const pp_fixit *fx, int nfx, const char *fmt, ...)
{
	pp_dinfo d = { "warning", loc, r, nr, fx, nfx, NULL, 0 };
	char msg[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	return pp_warnx(c->p, flag, on, groups, &d, "%s", msg);
}

VPRIV const char *idname(uint32_t id, int *len)
{
	*len = (int)vintern_ents[id].len;
	return vintern_ents[id].s;
}
VPRIV int idlen_(uint32_t id) { return (int)vintern_ents[id].len; }
VPRIV const char *idstr_(uint32_t id) { return vintern_ents[id].s; }
