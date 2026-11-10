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
	pp_diag_emit(c->p, pp_diag_vbegin(c->p, what[0] == 'e' ? VDL_ERROR : what[0] == 'w' ? VDL_WARNING : VDL_NOTE, loc, fmt, ap));
}

__attribute__((format(printf, 3, 4)))
VPRIV void cc_error(cc *c, uint32_t loc, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
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
	pp_diag_pc = __builtin_return_address(0);
	va_list ap;

	error_limit(c);
	va_start(ap, fmt);
	diag(c, loc, "error", fmt, ap);
	va_end(ap);
	c->errors++;
	parse_jump(c);
}

/* after a parse error was reported: to the innermost recovery point, or out of the TU */
VPRIV void parse_jump(cc *c)
{
	if (c->rec)
		_longjmp(c->rec->jb, 1);
	longjmp(c->bail, 1);
}

/* clang's "expected ')'" with "to match this '('" at open */
VPRIV void expect_match_fail(cc *c, int kind, uint32_t open)
{
	const char *cl = kind == T_RPAREN ? ")" : kind == T_RSQUARE ? "]" : "}";
	const char *op = kind == T_RPAREN ? "(" : kind == T_RSQUARE ? "[" : "{";

	if (c->miss_loc != tok(c)->loc) {	/* nested ones missing at the same token: once */
		c->miss_loc = tok(c)->loc;
		report(c, "error", tok(c)->kind == T_EOF ? pp_main_eof(c->p) : tok(c)->loc, NULL, 0, NULL, 0, "expected '%s'", cl);
		note(c, open, "to match this '%s'", op);
	}
	skip_close(c, kind);
}

/* clang's BalancedDelimiterTracker: on as if the closer were there, after skipping up
   to it (balanced, stopping before a ';' or a stray closer) */
VPRIV void skip_close(cc *c, int kind)
{
	int k, depth = 0;

	for (;; next(c)) {
		k = tok(c)->kind;
		if (k == T_EOF || (!depth && k == T_SEMI))
			return;
		if (k == T_LPAREN || k == T_LSQUARE || k == T_LBRACE) {
			depth++;
		} else if (k == T_RPAREN || k == T_RSQUARE || k == T_RBRACE) {
			if (depth--)
				continue;
			if (k == kind)
				next(c);
			return;
		}
	}
}

/* ---------------- delayed bugs (D2.1) ---------------- */

VPRIV void delayed_bug(cc *c, uint32_t loc, const char *what)
{
	if (!c->dbug)
		c->dbug = what, c->dbug_loc = loc;
}

/* at the end of the TU: a delayed bug with no error reported is vcc's bug */
VPRIV void delayed_bug_check(cc *c)
{
	vdiag *d;

	if (!c->dbug || c->errors || pp_errors(c->p))
		return;
	d = pp_diag_begin(c->p, VDL_ERROR, c->dbug_loc, "internal error: %s (vcc stopped short here, expecting an error)",
			  c->dbug);
	d->id = "vcc_internal_delayed_bug";
	pp_diag_help(c->p, d, "this is a bug in vcc, not in your code: please report it with this file (vcc -E output)");
	pp_diag_emit(c->p, d);
	c->errors++;
}

/* ---------------- parser recovery ---------------- */

VPRIV void rec_save(cc *c, recov *r)
{
	r->prev = c->rec;
	r->depth = c->depth, r->ndops = c->ndops, r->ncase = c->ncase, r->case_base = c->case_base;
	r->dead = c->dead, r->fn = c->fn;
	r->in_loop = c->in_loop, r->in_switch = c->in_switch, r->in_stmtexpr = c->in_stmtexpr;
	r->unevaluated = c->unevaluated, r->in_struct = c->in_struct, r->ibr = c->ibr, r->in_tyname = c->in_tyname;
	r->switch_cases = c->switch_cases, r->switch_ty = c->switch_ty;
	r->sw_default = c->sw_default, r->has_pend = c->has_pend, r->for_head = c->for_head;
	r->again = UINT32_MAX;
	c->rec = r;
}

/* GCC's c_parser_skip_to_end_of_block_or_statement: through a ';' or a block it opens,
   or up to a '}' that closes the enclosing one (top: at file scope, past it) */
/* blk: a block closed at nest 0 ends the skip (GCC); else it goes on to a ';' (clang's
   SkipUntil(r_brace, StopAtSemi), as for a member: "int f() { ... } int x;") */
static void skip_nest(cc *c, bool top, int nest, bool blk)
{
	int par = 0;	/* braces; parentheses and brackets (a ';' in them is skipped, as clang's SkipUntil) */

	for (;;) {
		switch (tok(c)->kind) {
		case T_EOF:
			return;
		case T_LPAREN:
		case T_LSQUARE:
			par++;
			break;
		case T_RPAREN:
		case T_RSQUARE:
			par -= par > 0;
			break;
		case T_SEMI:
			if (!nest && !par) {
				next(c);
				return;
			}
			break;
		case T_LBRACE:
			nest++;
			break;
		case T_RBRACE:
			if (!nest) {
				if (top)
					next(c);
				return;
			}
			if (!--nest && blk) {
				next(c);
				return;
			}
			break;
		default:
			break;
		}
		next(c);
	}
}

VPRIV void skip_stmt(cc *c, bool top) { skip_nest(c, top, 0, true); }
VPRIV void skip_semi(cc *c) { skip_nest(c, false, 0, false); }

/* the rest of a for header: balanced, through a ')' that closes nothing (the header's,
   or a nested one's when the error was deeper), or up to a brace */
static void skip_for_head(cc *c)
{
	int depth = 0;

	for (;; next(c)) {
		int k = tok(c)->kind;

		if (k == T_EOF || k == T_LBRACE || k == T_RBRACE)
			return;
		if (k == T_LPAREN || k == T_LSQUARE)
			depth++;
		else if ((k == T_RPAREN || k == T_RSQUARE) && !depth--) {
			next(c);
			return;
		}
	}
}

/* the '}' an error stopped at closes nothing: it follows neither a statement (';',
   '}'), a '{', a ',' nor a label ("abort (});", "if }(x)"); skipped like clang's SkipUntil */
static bool stray_rbrace(cc *c)
{
	size_t n;
	const char *s = pp_src(c->p, c->prevloc, c->prevloc + 1, &n);

	return s && n == 1 && !strchr(";{}:,", *s);
}

/* after a parse error: the state at r, then the rest of the statement or declaration
   skipped: through a ';' or a block ('{ ... }') it opened, or up to a '}' that closes
   the one r is in (REC_TOP: at file scope that '}' is skipped too; REC_FOR: in a for
   header, through its ')': the body is parsed) */
VPRIV void rec_restore(cc *c, recov *r, int mode)
{
	bool top = mode == REC_TOP;

	while (c->depth > r->depth)
		scope_close(c);
	c->ndops = r->ndops, c->ncase = r->ncase, c->case_base = r->case_base;
	c->dead = r->dead, c->fn = r->fn;
	c->in_loop = r->in_loop, c->in_switch = r->in_switch, c->in_stmtexpr = r->in_stmtexpr;
	c->unevaluated = r->unevaluated, c->in_struct = r->in_struct, c->in_tyname = r->in_tyname;
	int open = is(c, T_SEMI) ? 0 : c->ibr - r->ibr;	/* initializer braces left open, unless a ';' ends them */

	c->ibr = r->ibr;
	c->switch_cases = r->switch_cases, c->switch_ty = r->switch_ty;
	c->sw_default = r->sw_default, c->has_pend = r->has_pend, c->for_head = r->for_head;
	c->rec = r;
	if (c->decl_name && c->decl_depth == c->depth && !c->in_struct) {
		/* clang: a declaration that failed after its name still declares it, invalid,
		   so its uses say nothing more */
		uint32_t s = lookup(c, c->decl_name);

		if (!s || c->syms[s].depth != c->depth) {
			s = sym_new(c, S_VAR, c->decl_name, prim(TY_INT), c->decl_loc);
			c->syms[s].flags |= SF_INVALID;
			delayed_bug(c, c->decl_loc, "an invalid declaration without an error");
			sym_bind(c, s);
		}
	}
	c->decl_name = 0;
	dc_kill(c);	/* no declaration snapshots after an error */
	if (tok(c)->loc == r->again) {	/* no progress since the last one: past this token */
		if (is(c, T_EOF))
			longjmp(c->bail, 1);
		next(c);
	}
	if (mode == REC_FOR)
		skip_for_head(c);
	else {
		if (is(c, T_RBRACE) && !top && stray_rbrace(c))
			next(c);
		skip_nest(c, top, open, true);
		if (!top && iskw(c, KW_else))
			next(c);	/* the skipped statement was an if (or its body): on with its else branch */
	}
	r->again = tok(c)->loc;	/* where parsing resumes */
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
		pp_diag_fix(c->p, d, fx[i].b, fx[i].e, fx[i].s, VDA_UNSPEC);
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
		error_limit(c), c->errors++;
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
	"c23-extensions", "c2y-extensions", "extra-semi", "gnu-conditional-omitted-operand", "gnu-empty-struct",
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
		c->dms = NULL;
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

VPRIV const char *idname(uint32_t id, int *len)
{
	*len = (int)vintern_ents[id].len;
	return vintern_ents[id].s;
}
VPRIV int idlen_(uint32_t id) { return (int)vintern_ents[id].len; }
VPRIV const char *idstr_(uint32_t id) { return vintern_ents[id].s; }
