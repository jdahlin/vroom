/* vcc front end: parser recovery, the jump out of a parse error and the delayed bugs */
#include "ccimpl.h"

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
	const char *cl = kind == LEX_TOKEN_RIGHT_PAREN ? ")" : kind == LEX_TOKEN_RIGHT_SQUARE ? "]" : "}";
	const char *op = kind == LEX_TOKEN_RIGHT_PAREN ? "(" : kind == LEX_TOKEN_RIGHT_SQUARE ? "[" : "{";

	if (c->miss_loc != tok(c)->loc) {	/* nested ones missing at the same token: once */
		c->miss_loc = tok(c)->loc;
		report(c, "error", tok(c)->kind == LEX_TOKEN_EOF ? pp_main_file_end(c->p) : tok(c)->loc, NULL, 0, NULL, 0, "expected '%s'", cl);
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
		if (k == LEX_TOKEN_EOF || (!depth && k == LEX_TOKEN_SEMICOLON))
			return;
		if (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_LEFT_SQUARE || k == LEX_TOKEN_LEFT_BRACE) {
			depth++;
		} else if (k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_RIGHT_SQUARE || k == LEX_TOKEN_RIGHT_BRACE) {
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
	diag_record *d;

	if (!c->dbug || c->errors || pp_error_count(c->p))
		return;
	d = diag_record_begin(c->p, DIAG_LEVEL_ERROR, c->dbug_loc, "internal error: %s (vcc stopped short here, expecting an error)",
			  c->dbug);
	d->id = "vcc_internal_delayed_bug";
	diag_record_help_set(c->p, d, "this is a bug in vcc, not in your code: please report it with this file (vcc -E output)");
	diag_record_emit(c->p, d);
	c->errors++;
}

/* ---------------- parser recovery ---------------- */

VPRIV void rec_save(cc *c, recov *r)
{
	r->prev = c->rec;
	r->depth = c->depth, r->ndops = c->ndops, r->ncase = c->ncase, r->case_base = c->case_base;
	r->dead = c->dead, r->fn = c->fn, r->constexpr_init = c->constexpr_init;
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
		case LEX_TOKEN_EOF:
			return;
		case LEX_TOKEN_LEFT_PAREN:
		case LEX_TOKEN_LEFT_SQUARE:
			par++;
			break;
		case LEX_TOKEN_RIGHT_PAREN:
		case LEX_TOKEN_RIGHT_SQUARE:
			par -= par > 0;
			break;
		case LEX_TOKEN_SEMICOLON:
			if (!nest && !par) {
				next(c);
				return;
			}
			break;
		case LEX_TOKEN_LEFT_BRACE:
			nest++;
			break;
		case LEX_TOKEN_RIGHT_BRACE:
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

		if (k == LEX_TOKEN_EOF || k == LEX_TOKEN_LEFT_BRACE || k == LEX_TOKEN_RIGHT_BRACE)
			return;
		if (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_LEFT_SQUARE)
			depth++;
		else if ((k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_RIGHT_SQUARE) && !depth--) {
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
	const char *s = pp_source_text(c->p, c->prevloc, c->prevloc + 1, &n);

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
	c->dead = r->dead, c->fn = r->fn, c->constexpr_init = r->constexpr_init;
	c->in_loop = r->in_loop, c->in_switch = r->in_switch, c->in_stmtexpr = r->in_stmtexpr;
	c->unevaluated = r->unevaluated, c->in_struct = r->in_struct, c->in_tyname = r->in_tyname;
	int open = is(c, LEX_TOKEN_SEMICOLON) ? 0 : c->ibr - r->ibr;	/* initializer braces left open, unless a ';' ends them */

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
		if (is(c, LEX_TOKEN_EOF))
			longjmp(c->bail, 1);
		next(c);
	}
	if (mode == REC_FOR)
		skip_for_head(c);
	else {
		if (is(c, LEX_TOKEN_RIGHT_BRACE) && !top && stray_rbrace(c))
			next(c);
		skip_nest(c, top, open, true);
		if (!top && iskw(c, KW_else))
			next(c);	/* the skipped statement was an if (or its body): on with its else branch */
	}
	r->again = tok(c)->loc;	/* where parsing resumes */
}
