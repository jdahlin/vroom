/* vcc front end: what attributes promise, checked at their uses: deprecated and unavailable
   (-Wdeprecated-declarations), nonnull (-Wnonnull), warn_unused_result / nodiscard
   (-Wunused-result), as clang reports them */
#include "ccimpl.h"

static const char *const w_depr[] = { "deprecated", NULL };
static const char *const w_nonnull[] = { "format", "most", "all", NULL };
static const char *const w_result[] = { "unused-value", "unused", "most", "all", NULL };

static const char *msg_of(cc *c, uint32_t m)
{
	return m ? c->strs[m - 1].s : NULL;
}

/* a use of sym s at loc; deprecated warns, unavailable is an error (clang: not inside a
   function that is itself deprecated or unavailable) */
VPRIV void depr_use(cc *c, uint32_t s, uint32_t loc)
{
	const sym *y = &c->syms[s];

	if (y->attrs && (!c->fn || c->fn != s))
		depr_report(c, y->name, c->attr[y->attrs].ext, loc);
}

/* name, with the attribute record c->attrx[x], used at loc */
VPRIV void depr_report(cc *c, uint32_t name, uint32_t x, uint32_t loc)
{
	const attrx *a = &c->attrx[x];
	const char *m;

	if (!a->depr || (c->fn && (c->syms[c->fn].flags & SF_DEPR) && c->syms[c->fn].attrs &&
			 attrs_depr(c, &c->attr[c->syms[c->fn].attrs]) >= a->depr))
		return;
	m = msg_of(c, a->deprmsg);
	if (a->depr == 2) {
		cc_error(c, loc, "'%.*s' is unavailable%s%s", IDF(name), m && *m ? ": " : "", m && *m ? m : "");
		note(c, a->deprloc, "'%.*s' has been explicitly marked unavailable here", IDF(name));
		return;
	}
	vdiag *d = cc_warn_begin(c, "deprecated-declarations", true, w_depr, loc, "'%.*s' is deprecated%s%s",
				 IDF(name), m && *m ? ": " : "", m && *m ? m : "");

	if (d) {
		pp_diag_note(c->p, d, a->deprloc, "'%.*s' has been explicitly marked deprecated here", IDF(name));
		pp_diag_emit(c->p, d);
	}
}

static uint32_t strip_implicit(cc *c, uint32_t n)
{
	while (N(n)->op == N_CAST && !c->span[n].e)
		n = N(n)->a;
	return n;
}

/* clang's CheckNonNullExpr: a constant that is false as a condition */
static bool null_arg(cc *c, uint32_t n)
{
	cval v;

	return (is_ptr(c, N(n)->ty) || is_integer(c, N(n)->ty)) && cc_eval(c, n, &v) && v.kind == CV_INT && v.i == 0;
}

/* a call of function fs with its n converted arguments, ')' at rp */
VPRIV void nonnull_call(cc *c, uint32_t fs, const uint32_t *args, uint32_t n, uint32_t rp)
{
	const sym *f = &c->syms[fs];
	uint64_t m = f->attrs ? c->attrx[c->attr[f->attrs].ext].nonnull : 0;
	const ctype *ft = T(c, f->ty);
	uint32_t np = ft->kind == TY_FUNC ? ft->n : 0;

	if (quiet_rt(c))
		return;
	for (uint32_t i = 0; i < n && i < np; i++) {
		uint32_t ps = f->psyms ? c->lists[f->psyms - 1 + i] : 0, a;
		bool all = m == ~(uint64_t)0 && is_ptr(c, c->params[ft->aux + i]);
		bool want = all || (m != ~(uint64_t)0 && i < 64 && ((m >> i) & 1)) ||
			    (ps && (c->syms[ps].flags & SF_NONNULL));
		pp_span r;
		vdiag *d;

		if (!want)
			continue;
		a = strip_implicit(c, args[i]);
		if (!null_arg(c, a))
			continue;
		r = rng(c, a);
		/* (clang: at the call's ')' for the function's plain nonnull, else at the argument) */
		d = cc_warn_begin(c, "nonnull", true, w_nonnull, all && !(ps && (c->syms[ps].flags & SF_NONNULL)) ? rp : r.b,
				  "null passed to a callee that requires a non-null argument");
		if (!d)
			continue;
		pp_diag_label(c->p, d, r.b, r.e, NULL);
		pp_diag_help(c->p, d, "parameter %u of '%.*s' is declared nonnull", i + 1, IDF(f->name));
		rt_emit(c, d);
	}
}

/* a discarded call of a warn_unused_result / nodiscard function (discard() in unused.c) */
VPRIV void result_discarded(cc *c, uint32_t e)
{
	uint32_t f = strip_implicit(c, N(e)->a);
	const sym *y;
	const attrx *a;
	const char *m;
	pp_span r;
	vdiag *d;

	if (N(f)->op == N_ADDR)
		f = N(f)->a;
	if (N(f)->op != N_VAR || !(c->syms[N(f)->a].flags & SF_WUR))
		return;
	y = &c->syms[N(f)->a];
	a = y->attrs ? &c->attrx[c->attr[y->attrs].ext] : NULL;
	if (!a || !a->wur)
		return;
	m = msg_of(c, a->wurmsg);
	r = rng(c, f);
	d = cc_warn_begin(c, "unused-result", true, w_result, r.b,
			  "ignoring return value of function declared with '%s' attribute%s%s",
			  a->wur == 1 ? "warn_unused_result" : "nodiscard", m && *m ? ": " : "", m && *m ? m : "");
	if (!d)
		return;
	pp_diag_label(c->p, d, r.b, r.e, NULL);
	pp_diag_help(c->p, d, "use the result, or cast the call to (void) if dropping it is intended");
	pp_diag_emit(c->p, d);
}
