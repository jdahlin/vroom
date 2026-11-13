/* vcc front end: diagnostics in clang's format, the report entries and their flags */
#include "ccimpl.h"

/* ---------------- diagnostics ---------------- */

/* Clang's implicit limit permits 19 errors plus the fatal record; an explicit
 * -ferror-limit=N permits N errors before the fatal (0 means unlimited). */
VPRIV void error_limit(cc *c)
{
	int lim = pp_option_error_limit(c->p);

	if (!lim)
		lim = 19;
	if (lim > 0 && pp_error_count(c->p) >= lim) {
		diag_info d = { "fatal error", UINT32_MAX, NULL, 0, NULL, 0, NULL, 0 };

		diag_report_extended(c->p, &d, "too many errors emitted, stopping now [-ferror-limit=]");
		longjmp(c->bail, 1);
	}
}

VPRIV void diag(cc *c, uint32_t loc, const char *what, const char *fmt, va_list ap)
{
	c->ndiag++;
	diag_record_emit(c->p, diag_record_begin_va(c->p, what[0] == 'e' ? DIAG_LEVEL_ERROR : what[0] == 'w' ? DIAG_LEVEL_WARNING : DIAG_LEVEL_NOTE, loc, fmt, ap));
}

/* -Winvalid-pp-token for the unknown tokens read ahead (they always end in an error: here, not per token) */
static void unknown_toks(cc *c)
{
	for (int i = 0; i < c->ntk; i++) {
		const ptok *t = &c->tk[i];

		if (t->kind == LEX_TOKEN_UNKNOWN && !(t->flags & PP_TOKEN_FLAG_FROM_MACRO) && t->loc != c->unk_loc) {
			c->unk_loc = t->loc;
			pp_token_invalid_report(c->p, t->s, t->len, t->loc);
		}
	}
}

__attribute__((format(printf, 3, 4)))
VPRIV void cc_error(cc *c, uint32_t loc, const char *fmt, ...)
{
	diag_report_site_pc = __builtin_return_address(0);
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
	diag_report_site_pc = __builtin_return_address(0);
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
static void take_dms(cc *c, diag_record *d)
{
	if (unlikely(c->dms != NULL)) {
		if (*c->dms)
			diag_record_macro_set(d, c->dms, (uint32_t)strlen(c->dms));
		c->dms = NULL;
	}
	if (unlikely(c->dfx.s != NULL)) {
		diag_record_fixit_add(c->p, d, c->dfx.b, c->dfx.e, c->dfx.s, DIAG_APPLICABILITY_MACHINE);
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
	_Static_assert(sizeof(s) / sizeof(s[0]) == LEX_TOKEN_KIND_COUNT - LEX_TOKEN_LEFT_SQUARE, "punctuators");
	return k >= LEX_TOKEN_LEFT_SQUARE && k < LEX_TOKEN_KIND_COUNT ? s[k - LEX_TOKEN_LEFT_SQUARE] : "";
}

static diag_record *rec(cc *c, int level, uint32_t loc, const diag_span *r, int nr, const diag_fixit *fx, int nfx,
		  const char *fmt, va_list ap)
{
	diag_record *d = diag_record_begin_va(c->p, level, loc, fmt, ap);

	take_dms(c, d);
	for (int i = 0; i < nr; i++)
		diag_record_label_add(c->p, d, r[i].b, r[i].e, NULL);
	for (int i = 0; i < nfx; i++)
		diag_record_fixit_add(c->p, d, fx[i].b, fx[i].e, fx[i].s, fx[i].app);
	return d;
}

/* a diagnostic with ranges and fix-its (clang's rendering, pp.c) */
__attribute__((format(printf, 8, 9)))
VPRIV void report(cc *c, const char *what, uint32_t loc, const diag_span *r, int nr,
		   const diag_fixit *fx, int nfx, const char *fmt, ...)
{
	diag_report_site_pc = __builtin_return_address(0);
	int lv = what[0] == 'e' ? DIAG_LEVEL_ERROR : what[0] == 'w' ? DIAG_LEVEL_WARNING : DIAG_LEVEL_NOTE;
	va_list ap;

	if (lv == DIAG_LEVEL_ERROR)
		unknown_toks(c), error_limit(c), c->errors++;
	if (lv != DIAG_LEVEL_NOTE)
		c->ndiag++;
	va_start(ap, fmt);
	diag_record_emit(c->p, rec(c, lv, loc, r, nr, fx, nfx, fmt, ap));
	va_end(ap);
}

__attribute__((format(printf, 3, 4)))
VPRIV void note(cc *c, uint32_t loc, const char *fmt, ...)
{
	diag_report_site_pc = __builtin_return_address(0);
	va_list ap;

	va_start(ap, fmt);
	diag_record_emit(c->p, diag_record_begin_va(c->p, DIAG_LEVEL_NOTE, loc, fmt, ap));
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
VPRIV void rt_emit(cc *c, diag_record *d)
{
	if (!c->fn) {
		diag_record_emit(c->p, d);
		return;
	}
	if (c->nrtq == c->rtqcap) {
		c->rtqcap = c->rtqcap ? c->rtqcap * 2 : 8;
		c->rtq = realloc(c->rtq, c->rtqcap * sizeof(diag_record *));
	}
	c->rtq[c->nrtq++] = d;
}

/* the function's body is done */
VPRIV void rt_flush(cc *c)
{
	for (uint32_t i = 0; i < c->nrtq; i++)
		diag_record_emit(c->p, c->rtq[i]);
	c->nrtq = 0;
}

/* a warning of clang's flag (on: on by default); false if it is off */
__attribute__((format(printf, 10, 11)))
VPRIV bool cc_warnx(cc *c, const char *flag, bool on, const char *const *groups, uint32_t loc,
		  const diag_span *r, int nr, const diag_fixit *fx, int nfx, const char *fmt, ...)
{
	diag_report_site_pc = __builtin_return_address(0);
	int lv = diag_warning_level(c->p, flag, on, groups, loc);
	diag_record *d;
	va_list ap;

	if (lv < 0) {
		c->dms = NULL;
		return false;
	}
	va_start(ap, fmt);
	d = rec(c, lv, loc, r, nr, fx, nfx, fmt, ap);
	va_end(ap);
	diag_record_flag_set(d, flag);
	d->werror = lv == DIAG_LEVEL_ERROR;
	diag_record_emit(c->p, d);
	return true;
}

/* A default error that participates in named -W controls and pragmas. */
__attribute__((format(printf, 7, 8)))
VPRIV void cc_errorx(cc *c, const char *flag, const char *const *groups, uint32_t loc,
                   const diag_span *r, int nr, const char *fmt, ...)
{
	int lv = diag_error_level(c->p, flag, groups, loc);
	va_list ap;

	if (lv < 0) { c->dms = NULL; return; }
	diag_report_site_pc = __builtin_return_address(0);
	if (lv == DIAG_LEVEL_ERROR)
		unknown_toks(c), error_limit(c), c->errors++;
	c->ndiag++;
	va_start(ap, fmt);
	diag_record *d = rec(c, lv, loc, r, nr, NULL, 0, fmt, ap);
	va_end(ap);
	diag_record_flag_set(d, flag);
	d->werror = lv == DIAG_LEVEL_ERROR && diag_warning_error_named(c->p, flag, groups) > 0;
	diag_record_emit(c->p, d);
}

/* a warning record of clang's flag to fill in (labels, help) and diag_record_emit; NULL if off */
__attribute__((format(printf, 6, 7)))
VPRIV diag_record *cc_warn_begin(cc *c, const char *flag, bool on, const char *const *groups, uint32_t loc, const char *fmt, ...)
{
	int lv = diag_warning_level(c->p, flag, on, groups, loc);
	diag_record *d;
	va_list ap;

	if (lv < 0) {
		c->dms = NULL;
		return NULL;
	}
	if (lv == DIAG_LEVEL_ERROR)
		error_limit(c), c->errors++;
	c->ndiag++;
	va_start(ap, fmt);
	d = diag_record_begin_va(c->p, lv, loc, fmt, ap);
	va_end(ap);
	take_dms(c, d);
	diag_record_flag_set(d, flag);
	d->werror = lv == DIAG_LEVEL_ERROR;
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
		const char *const *wa = diag_warning_arguments(c->p, &nw);

		c->nenable = diag_warning_enabled_count_reference(c->p);
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
		return *c->nenable != 0;	/* (and then diag_warning_level decides by location) */
	if (diag_pragma_count(c->p))
		return true;	/* (a pragma may turn it off somewhere: diag_warning_level decides) */
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
		c->next_v[i] = diag_warning_state(c->p, flag, false, groups) != 0;
		c->next_n++;
		return c->next_v[i];
	}
	return true;
}

VPRIV void ext_warn(cc *c, const char *flag, const char *const *groups, uint32_t loc, const char *fmt, ...)
{
	int lv;
	diag_record *d;
	va_list ap;

	diag_report_site_pc = __builtin_return_address(0);
	if (!ext_on(c, flag, groups) || (lv = diag_warning_level(c->p, flag, false, groups, loc)) < 0) {
		c->dms = NULL, c->dfx.s = NULL;
		return;
	}
	if (lv == DIAG_LEVEL_ERROR)
		error_limit(c), c->errors++;
	c->ndiag++;
	va_start(ap, fmt);
	d = diag_record_begin_va(c->p, lv, loc, fmt, ap);
	va_end(ap);
	take_dms(c, d);
	diag_record_flag_set(d, flag);
	d->werror = lv == DIAG_LEVEL_ERROR;
	diag_record_emit(c->p, d);
}

/* a warning for the end of the function body (clang's DiagRuntimeBehavior); ms: the macro note's spelling */
__attribute__((format(printf, 9, 10)))
VPRIV void rt_defer(cc *c, const char *flag, const char *const *groups, uint32_t loc, const diag_span *r, int nr,
		    const char *ms, uint32_t mlen, const char *fmt, ...)
{
	va_list ap;
	int lv;
	diag_record *d;

	diag_report_site_pc = __builtin_return_address(0);
	if ((lv = diag_warning_level(c->p, flag, true, groups, loc)) < 0)
		return;
	va_start(ap, fmt);
	d = rec(c, lv, loc, r, nr, NULL, 0, fmt, ap);	/* (as cc_warnx: its macro notes too) */
	va_end(ap);
	diag_record_flag_set(d, flag);
	d->werror = lv == DIAG_LEVEL_ERROR;
	if (ms) {	/* (a copy: the record waits for the body's end) */
		char *m = halloc(c, mlen + 1);

		memcpy(m, ms, mlen), m[mlen] = 0;
		diag_record_macro_set(d, m, mlen);
	}
	rt_emit(c, d);
}
