/* vcc front end: diagnostics in clang's format, the report entries and their flags */
#include "ccimpl.h"

/* ---------------- diagnostics ---------------- */

/* Clang's implicit limit permits 19 errors plus the fatal record; an explicit
 * -ferror-limit=N permits N errors before the fatal (0 means unlimited). */
VPRIV void error_limit(cc *c)
{
	int lim = pp_error_limit(c->p);

	if (!lim)
		lim = 19;
	if (lim > 0 && pp_errors(c->p) >= lim) {
		pp_dinfo d = { "fatal error", UINT32_MAX, NULL, 0, NULL, 0, NULL, 0 };

		pp_diagx(c->p, &d, "too many errors emitted, stopping now [-ferror-limit=]");
		longjmp(c->bail, 1);
	}
}

VPRIV void diag(cc *c, uint32_t loc, const char *what, const char *fmt, va_list ap)
{
	c->ndiag++;
	pp_diag_emit(c->p, pp_diag_vbegin(c->p, what[0] == 'e' ? VDL_ERROR : what[0] == 'w' ? VDL_WARNING : VDL_NOTE, loc, fmt, ap));
}

/* -Winvalid-pp-token for the unknown tokens read ahead (they always end in an error: here, not per token) */
static void unknown_toks(cc *c)
{
	for (int i = 0; i < c->ntk; i++) {
		const ptok *t = &c->tk[i];

		if (t->kind == T_UNKNOWN && !(t->flags & PF_MACRO) && t->loc != c->unk_loc) {
			c->unk_loc = t->loc;
			pp_invalid_tok(c->p, t->s, t->len, t->loc);
		}
	}
}

__attribute__((format(printf, 3, 4)))
VPRIV void cc_error(cc *c, uint32_t loc, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	va_list ap;

	unknown_toks(c);
	error_limit(c);
	va_start(ap, fmt);
	diag(c, loc, "error", fmt, ap);
	va_end(ap);
	c->errors++;
}

__attribute__((format(printf, 3, 4), noreturn))
VPRIV void fatal(cc *c, uint32_t loc, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	va_list ap;

	unknown_toks(c);
	error_limit(c);
	va_start(ap, fmt);
	diag(c, loc, "error", fmt, ap);
	va_end(ap);
	c->errors++;
	parse_jump(c);
}

/* a record with clang's ranges and fix-its */
/* c->dms to the record (pp moves the location to the macro invocation and adds the
   "expanded from macro" notes at the body token so spelled) */
static void take_dms(cc *c, vdiag *d)
{
	if (unlikely(c->dms != NULL)) {
		if (*c->dms)
			pp_diag_macro(d, c->dms, (uint32_t)strlen(c->dms));
		c->dms = NULL;
	}
	if (unlikely(c->dfx.s != NULL)) {
		pp_diag_fix(c->p, d, c->dfx.b, c->dfx.e, c->dfx.s, VDA_MACHINE);
		c->dfx.s = NULL;
	}
}

/* the spelling of punctuator kind k ("" for others) */
VPRIV const char *punct_spell(int k)
{
	static const char *const s[] = {
		"[", "]", "(", ")", "{", "}", ".", "...", "&", "&&", "&=", "*", "*=", "+", "++", "+=", "-", "->", "--",
		"-=", "~", "!", "!=", "/", "/=", "%", "%=", "<", "<<", "<=", "<<=", ">", ">>", ">=", ">>=", "^", "^=",
		"|", "||", "|=", "?", ":", "::", ";", "=", "==", ",", "#", "##",
	};
	_Static_assert(sizeof(s) / sizeof(s[0]) == T_NKINDS - T_LSQUARE, "punctuators");
	return k >= T_LSQUARE && k < T_NKINDS ? s[k - T_LSQUARE] : "";
}

static vdiag *rec(cc *c, int level, uint32_t loc, const pp_span *r, int nr, const pp_fixit *fx, int nfx,
		  const char *fmt, va_list ap)
{
	vdiag *d = pp_diag_vbegin(c->p, level, loc, fmt, ap);

	take_dms(c, d);
	for (int i = 0; i < nr; i++)
		pp_diag_label(c->p, d, r[i].b, r[i].e, NULL);
	for (int i = 0; i < nfx; i++)
		pp_diag_fix(c->p, d, fx[i].b, fx[i].e, fx[i].s, fx[i].app);
	return d;
}

/* a diagnostic with ranges and fix-its (clang's rendering, pp.c) */
__attribute__((format(printf, 8, 9)))
VPRIV void report(cc *c, const char *what, uint32_t loc, const pp_span *r, int nr,
		   const pp_fixit *fx, int nfx, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	int lv = what[0] == 'e' ? VDL_ERROR : what[0] == 'w' ? VDL_WARNING : VDL_NOTE;
	va_list ap;

	if (lv == VDL_ERROR)
		unknown_toks(c), error_limit(c), c->errors++;
	if (lv != VDL_NOTE)
		c->ndiag++;
	va_start(ap, fmt);
	pp_diag_emit(c->p, rec(c, lv, loc, r, nr, fx, nfx, fmt, ap));
	va_end(ap);
}

__attribute__((format(printf, 3, 4)))
VPRIV void note(cc *c, uint32_t loc, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	va_list ap;

	va_start(ap, fmt);
	pp_diag_emit(c->p, pp_diag_vbegin(c->p, VDL_NOTE, loc, fmt, ap));
	va_end(ap);
}

/* clang's note on an incomplete struct, union or enum: where it was declared */
VPRIV void fwd_note(cc *c, qty t)
{
	int k = KIND(c, t);

	while (k == TY_ARRAY)
		t = T(c, t)->base, k = KIND(c, t);
	if ((k == TY_STRUCT || k == TY_UNION || k == TY_ENUM) && c->tags[T(c, t)->aux].loc)
		note(c, c->tags[T(c, t)->aux].loc, "forward declaration of '%s'", tyname(c, t));
}

const char *const w_all[] = { "all", "most", NULL };
const char *const w_conv[] = { "conversion", NULL };
const char *const w_selfassign[] = { "all", "most", NULL };

/* a warning clang gives through DiagRuntimeBehavior: inside a function it waits for the
   end of the body (clang's PossiblyUnreachableDiags), elsewhere it goes out now */
VPRIV void rt_emit(cc *c, vdiag *d)
{
	if (!c->fn) {
		pp_diag_emit(c->p, d);
		return;
	}
	if (c->nrtq == c->rtqcap) {
		c->rtqcap = c->rtqcap ? c->rtqcap * 2 : 8;
		c->rtq = realloc(c->rtq, c->rtqcap * sizeof(vdiag *));
	}
	c->rtq[c->nrtq++] = d;
}

/* the function's body is done */
VPRIV void rt_flush(cc *c)
{
	for (uint32_t i = 0; i < c->nrtq; i++)
		pp_diag_emit(c->p, c->rtq[i]);
	c->nrtq = 0;
}

/* a warning of clang's flag (on: on by default); false if it is off */
__attribute__((format(printf, 10, 11)))
VPRIV bool cc_warnx(cc *c, const char *flag, bool on, const char *const *groups, uint32_t loc,
		  const pp_span *r, int nr, const pp_fixit *fx, int nfx, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	int lv = pp_diag_wlevel(c->p, flag, on, groups, loc);
	vdiag *d;
	va_list ap;

	if (lv < 0) {
		c->dms = NULL;
		return false;
	}
	va_start(ap, fmt);
	d = rec(c, lv, loc, r, nr, fx, nfx, fmt, ap);
	va_end(ap);
	pp_diag_flag(d, flag);
	d->werror = lv == VDL_ERROR;
	pp_diag_emit(c->p, d);
	return true;
}

/* A default error that participates in named -W controls and pragmas. */
__attribute__((format(printf, 7, 8)))
VPRIV void cc_errorx(cc *c, const char *flag, const char *const *groups, uint32_t loc,
                   const pp_span *r, int nr, const char *fmt, ...)
{
	int lv = pp_diag_elevel(c->p, flag, groups, loc);
	va_list ap;

	if (lv < 0) { c->dms = NULL; return; }
	pp_diag_pc = __builtin_return_address(0);
	if (lv == VDL_ERROR)
		unknown_toks(c), error_limit(c), c->errors++;
	c->ndiag++;
	va_start(ap, fmt);
	vdiag *d = rec(c, lv, loc, r, nr, NULL, 0, fmt, ap);
	va_end(ap);
	pp_diag_flag(d, flag);
	d->werror = lv == VDL_ERROR && pp_werror_named(c->p, flag, groups) > 0;
	pp_diag_emit(c->p, d);
}

/* a warning record of clang's flag to fill in (labels, help) and pp_diag_emit; NULL if off */
__attribute__((format(printf, 6, 7)))
VPRIV vdiag *cc_warn_begin(cc *c, const char *flag, bool on, const char *const *groups, uint32_t loc, const char *fmt, ...)
{
	int lv = pp_diag_wlevel(c->p, flag, on, groups, loc);
	vdiag *d;
	va_list ap;

	if (lv < 0) {
		c->dms = NULL;
		return NULL;
	}
	if (lv == VDL_ERROR)
		error_limit(c), c->errors++;
	c->ndiag++;
	va_start(ap, fmt);
	d = pp_diag_vbegin(c->p, lv, loc, fmt, ap);
	va_end(ap);
	take_dms(c, d);
	pp_diag_flag(d, flag);
	d->werror = lv == VDL_ERROR;
	return d;
}

/* clang's extension warnings (its ext_* diagnostics): off unless -pedantic or the flag or one
   of its groups is named; whether they are off is kept per flag (wargs are fixed for the TU) */
const char *const w_gnu[] = { "gnu", "pedantic", NULL };
const char *const w_ped[] = { "pedantic", NULL };

/* every flag ext_on is asked about, and every group they are in: whether any can be on */
static const char *const ext_flags[] = {
	"c23-extensions", "c2y-extensions", "extra-semi", "gnu-conditional-omitted-operand", "gnu-compound-literal-initializer", "gnu-empty-struct",
	"gnu-label-as-value", "gnu-pointer-arith", "gnu-statement-expression",
	"gnu-statement-expression-from-macro-expansion", "language-extension-token", "pedantic", "pointer-arith",
	"zero-length-array", NULL
};
static const char *const ext_groups[] = { "pedantic", "gnu", "pointer-arith", "gnu-statement-expression", NULL };

VPRIV bool ext_on_slow(cc *c, const char *flag, const char *const *groups)
{
	uint32_t i;

	if (c->ext_off < 0) {	/* off unless an option names one of them ("-Wx", "-Werror=x") */
		int nw;
		const char *const *wa = pp_diag_wargs(c->p, &nw);

		c->nenable = pp_diag_nenable_ref(c->p);
		c->ext_off = 1;
		for (int k = 0; k < nw && c->ext_off; k++) {
			const char *a = wa[k];

			if (a[1] != 'W' || !strncmp(a, "-Wno-", 5))
				continue;
			a += strncmp(a, "-Werror=", 8) ? 2 : 8;
			if (!strcmp(a, "everything"))
				c->ext_off = 0;
			for (i = 0; ext_flags[i] && c->ext_off; i++)
				c->ext_off = strcmp(a, ext_flags[i]) != 0;
			for (i = 0; ext_groups[i] && c->ext_off; i++)
				c->ext_off = strcmp(a, ext_groups[i]) != 0;
		}
	}
	if (c->ext_off > 0)	/* off on the command line: only a "warning"/"error" pragma turns it on */
		return *c->nenable != 0;	/* (and then pp_diag_wlevel decides by location) */
	if (pp_diag_npragma(c->p))
		return true;	/* (a pragma may turn it off somewhere: pp_diag_wlevel decides) */
	for (i = 0; i < c->next_n; i++)
		if (c->next_k[i] == flag)
			return c->next_v[i];
	if (i < sizeof(c->next_k) / sizeof(c->next_k[0])) {
		uint32_t k = 0;

		while (ext_flags[k] && strcmp(ext_flags[k], flag))
			k++;
		if (!ext_flags[k]) {	/* (ext_off would hide it) */
			fprintf(stderr, "vcc: internal error: ext_on(\"%s\") is not in ext_flags\n", flag);
			abort();
		}
		c->next_k[i] = flag;
		c->next_v[i] = pp_wstate(c->p, flag, false, groups) != 0;
		c->next_n++;
		return c->next_v[i];
	}
	return true;
}

VPRIV void ext_warn(cc *c, const char *flag, const char *const *groups, uint32_t loc, const char *fmt, ...)
{
	int lv;
	vdiag *d;
	va_list ap;

	pp_diag_pc = __builtin_return_address(0);
	if (!ext_on(c, flag, groups) || (lv = pp_diag_wlevel(c->p, flag, false, groups, loc)) < 0) {
		c->dms = NULL, c->dfx.s = NULL;
		return;
	}
	if (lv == VDL_ERROR)
		error_limit(c), c->errors++;
	c->ndiag++;
	va_start(ap, fmt);
	d = pp_diag_vbegin(c->p, lv, loc, fmt, ap);
	va_end(ap);
	take_dms(c, d);
	pp_diag_flag(d, flag);
	d->werror = lv == VDL_ERROR;
	pp_diag_emit(c->p, d);
}

/* a warning for the end of the function body (clang's DiagRuntimeBehavior); ms: the macro note's spelling */
__attribute__((format(printf, 9, 10)))
VPRIV void rt_defer(cc *c, const char *flag, const char *const *groups, uint32_t loc, const pp_span *r, int nr,
		    const char *ms, uint32_t mlen, const char *fmt, ...)
{
	va_list ap;
	int lv;
	vdiag *d;

	pp_diag_pc = __builtin_return_address(0);
	if ((lv = pp_diag_wlevel(c->p, flag, true, groups, loc)) < 0)
		return;
	va_start(ap, fmt);
	d = rec(c, lv, loc, r, nr, NULL, 0, fmt, ap);	/* (as cc_warnx: its macro notes too) */
	va_end(ap);
	pp_diag_flag(d, flag);
	d->werror = lv == VDL_ERROR;
	if (ms) {	/* (a copy: the record waits for the body's end) */
		char *m = halloc(c, mlen + 1);

		memcpy(m, ms, mlen), m[mlen] = 0;
		pp_diag_macro(d, m, mlen);
	}
	rt_emit(c, d);
}
