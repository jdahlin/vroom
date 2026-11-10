/* vcc front end: statements (asm statements included) */
#include "ccimpl.h"

/* ---------------- statements ---------------- */

VPRIV uint32_t label_ref(cc *c, uint32_t name, uint32_t loc)
{
	uint32_t l = dget(c->ldir, name);

	if (l)
		return l;
	l = c->nlabel++;
	memset(&c->labels[l], 0, sizeof(label));
	c->labels[l].name = name;
	c->labels[l].loc = loc;
	c->labels[l].shadow = 0;
	*dslot(c, c->ldir, name) = l;
	return l;
}

VPRIV uint32_t mkstmt(cc *c, int op, uint32_t loc, uint32_t a, uint32_t b, uint32_t cc_)
{
	uint32_t n = mk(c, op, prim(TY_VOID), loc);

	N(n)->a = a, N(n)->b = b, N(n)->c = cc_;
	return n;
}

/* -Wparentheses: "if (x = 1)" */
static void assign_cond_warn(cc *c, uint32_t e)
{
	if (N(e)->op == N_ASSIGN && !N(e)->aux && !(N(e)->flags & (NF_PAREN | NF_MACRO | NF_ERR))) {
		uint32_t op = N(e)->loc;
		pp_span r[2] = { rng(c, N(e)->a), rng(c, N(e)->b) };
		pp_span all = { r[0].b, r[1].e };
		pp_fixit p2[2] = { { all.b, all.b, "(" }, { all.e, all.e, ")" } };
		pp_fixit eq = { op, op + 1, "==" };

		if (cc_warnx(c, "parentheses", true, w_all, op, &all, 1, NULL, 0,
			  "using the result of an assignment as a condition without parentheses")) {
			pp_dinfo d1 = { "note", op, NULL, 0, p2, 2, NULL, 0 }, d2 = { "note", op, NULL, 0, &eq, 1, NULL, 0 };

			pp_diagx(c->p, &d1, "place parentheses around the assignment to silence this warning");
			pp_diagx(c->p, &d2, "use '==' to turn this assignment into an equality comparison");
		}
	}
}

/* -Wswitch: "enumeration value 'B' not handled in switch" (no default label) */
static void switch_enum_warn(cc *c, qty et, uint32_t cnd)
{
	const tag *t = &c->tags[T(c, et)->aux];
	uint32_t miss[3], nmiss = 0;
	char b[512];
	size_t k = 0;

	for (uint32_t i = 0; i < t->nenum; i++) {
		const sym *y = &c->syms[c->lists[t->enums + i]];
		bool seen = false;

		for (uint32_t j = c->case_base; j < c->ncase && !seen; j++)
			seen = c->cases[j].lo <= y->val && y->val <= c->cases[j].hi;
		/* an enumerator repeating an earlier one's value counts once */
		for (uint32_t j = 0; j < i && !seen; j++)
			seen = c->syms[c->lists[t->enums + j]].val == y->val;
		if (!seen) {
			if (nmiss < 3)
				miss[nmiss] = c->lists[t->enums + i];
			nmiss++;
		}
	}
	if (!nmiss)
		return;
	for (uint32_t i = 0; i < nmiss && i < 3; i++) {
		const char *sep = i == 0 ? "" : nmiss == 2 ? " and " : (i == 2 && nmiss == 3) ? ", and " : ", ";

		k += (size_t)snprintf(b + k, sizeof(b) - k, "%s'%.*s'", sep, IDF(c->syms[miss[i]].name));
		if (k >= sizeof(b))
			return;
	}
	if (nmiss > 3)
		snprintf(b + k, sizeof(b) - k, ", and %u more", nmiss - 3);
	{
		pp_span r = rng(c, cnd);

		cc_warnx(c, "switch", true, w_all, r.b, NULL, 0, NULL, 0, "enumeration value%s %s not handled in switch",
		      nmiss > 1 ? "s" : "", b);
	}
}

static uint32_t cond_of(cc *c, const char *kw)
{
	uint32_t e, open = tok(c)->loc;

	if (unlikely(!is(c, T_LPAREN)))
		lparen_fail(c, kw);
	next(c);
	e = decay(c, expr(c));
	assign_cond_warn(c, e);
	expect_close(c, T_RPAREN, open, e);
	return scalar_cond(c, e, "statement");
}

static uint32_t asm_string(cc *c)
{
	return string_index(c, 0);
}

static void asm_operands(cc *c, vec *ops, bool out)
{
	if (is(c, T_COLON) || is(c, T_COLONCOLON) || is(c, T_RPAREN))
		return;
	for (;;) {
		uint32_t name = 0, cons, e;

		if (accept(c, T_LSQUARE)) {
			if (is(c, T_IDENT))
				name = tok(c)->id;
			next(c);
			expect(c, T_RSQUARE, "']'");
		}
		cons = asm_string(c);
		expect(c, T_LPAREN, "'('");
		e = expr(c);
		expect(c, T_RPAREN, "')'");
		if (out) {
			if (!(N(e)->flags & NF_LVALUE))
				cc_error(c, N(e)->loc, "invalid lvalue in asm output");
		} else
			e = decay(c, e);
		vpush(c, ops, name);
		vpush(c, ops, cons);
		vpush(c, ops, e);
		if (!accept(c, T_COMMA))
			break;
	}
}

/* one ':' of an asm statement; "::" counts as two */
static int asm_colons(cc *c, int *pending)
{
	if (*pending) {
		(*pending)--;
		return 1;
	}
	if (accept(c, T_COLON))
		return 1;
	if (accept(c, T_COLONCOLON)) {
		*pending = 1;
		return 1;
	}
	return 0;
}

VPRIV uint32_t asm_stmt(cc *c, bool toplevel)
{
	uint32_t loc = tok(c)->loc, ai = c->nasm++, n;
	casm *a = &c->asms[ai];
	vec outs, ins, clob, labs;
	int pending = 0;

	memset(a, 0, sizeof(*a));
	next(c);
	for (;;) {
		int kw = kwof(tok(c));

		if (kw == KW_volatile || kw == KW___volatile__ || kw == KW___volatile)
			a->vol = 1;
		else if (kw == KW_inline || kw == KW___inline__ || kw == KW___inline)
			a->inl = 1;
		else if (kw == KW_goto)
			a->go = 1;
		else
			break;
		next(c);
	}
	vinit(&outs), vinit(&ins), vinit(&clob), vinit(&labs);
	expect(c, T_LPAREN, "'('");
	a->tmpl = asm_string(c);
	if (!toplevel && asm_colons(c, &pending)) {
		if (!pending)
			asm_operands(c, &outs, true);
		if (asm_colons(c, &pending)) {
			if (!pending)
				asm_operands(c, &ins, false);
			if (asm_colons(c, &pending)) {
				if (!pending && !is(c, T_COLON) && !is(c, T_COLONCOLON) && !is(c, T_RPAREN)) {
					do
						vpush(c, &clob, asm_string(c));
					while (accept(c, T_COMMA));
				}
				if (asm_colons(c, &pending)) {
					if (!is(c, T_RPAREN)) {
						do {
							if (!is(c, T_IDENT))
								fatal(c, tok(c)->loc, "expected identifier");
							vpush(c, &labs, label_ref(c, tok(c)->id, tok(c)->loc));
							next(c);
						} while (accept(c, T_COMMA));
					}
				}
			}
		}
	}
	expect(c, T_RPAREN, "')'");
	a = &c->asms[ai];
	a->nout = outs.n / 3;
	a->nin = ins.n / 3;
	a->ops = halloc(c, (a->nout + a->nin + 1) * sizeof(asmop));
	for (uint32_t i = 0; i < a->nout; i++)
		a->ops[i] = (asmop){ outs.v[3 * i], outs.v[3 * i + 1], outs.v[3 * i + 2] };
	for (uint32_t i = 0; i < a->nin; i++)
		a->ops[a->nout + i] = (asmop){ ins.v[3 * i], ins.v[3 * i + 1], ins.v[3 * i + 2] };
	a->nclob = clob.n;
	a->clob = halloc(c, (clob.n + 1) * sizeof(uint32_t));
	memcpy(a->clob, clob.v, clob.n * sizeof(uint32_t));
	a->nlabel = labs.n;
	a->labels = halloc(c, (labs.n + 1) * sizeof(uint32_t));
	memcpy(a->labels, labs.v, labs.n * sizeof(uint32_t));
	expect_semi(c, "after asm statement");
	n = mkstmt(c, N_ASM, loc, ai, 0, 0);
	return n;
}

VPRIV bool at_asm(cc *c)
{
	int kw = kwof(tok(c));

	return kw == KW_asm || kw == KW___asm__ || kw == KW___asm;
}

/* clang: an unknown identifier at the start of a statement that is a typo of a
   keyword ("retur x;", "stati int n;") is reported with the keyword and parsed as
   it (decls: a declaration may stand here).  Only before a token that cannot go on
   an expression, so a valid statement costs a lookup */
static bool stmt_kw_typo(cc *c, bool decls)
{
	ptok *t = tok(c);
	uint32_t kw;
	int k;

	if (t->kind != T_IDENT || kwof(t) || lookup(c, t->id))	/* declared: no peek */
		return false;
	k = peek(c, 1)->kind;
	if (k != T_IDENT && k != T_NUMBER && k != T_CHAR && !is_strtok(k) && k != T_LBRACE &&
	    k != T_SEMI && k != T_STAR)
		return false;
	t = tok(c);
	if (!(kw = typo_keyword(c, t->id, KWS_STMT)))
		return false;
	if (!decls && (kwclass[kwmap[kw]] & KC_DECL))
		return false;
	undeclared(c, "use of undeclared identifier", t->id, t->loc, false, KWS_STMT, &kw);
	tok(c)->id = kw;
	return true;
}

/* "size_t n = 1;", "FILE *fp;" with the type's header missing: clang reports the
   undeclared identifier and then each declarator's uses; vcc parses a declaration of an
   invalid type, so the uses say nothing more.  Only where an expression cannot go on
   (a name after a name), or "T *p" with T a standard type (cc/libnames.inc) */
static __attribute__((cold, noinline)) bool undeclared_type_decl(cc *c, vec *items)
{
	const ptok *t = tok(c);
	int i = 1, k;

	if (lookup(c, t->id) || (t->flags & PF_MACRO))
		return false;
	if (peek(c, 1)->kind == T_STAR) {
		if (!lib_header(c, t->id))
			return false;
		i = 2;
	}
	if (peek(c, i)->kind != T_IDENT || kwof(peek(c, i)))
		return false;
	k = peek(c, i + 1)->kind;
	if (k != T_EQUAL && k != T_SEMI && k != T_COMMA && k != T_LSQUARE)
		return false;
	c->undecl_ty = 1;
	declaration(c, items, false);
	c->undecl_ty = 0;
	return true;
}

/* the ';' after a part (e) of a for header; clang when it is missing: reported
   (unless e was invalid), then the rest of the part skipped up to a ';' or the ')' */
static void for_semi(cc *c, uint32_t e)
{
	if (likely(accept(c, T_SEMI)))
		return;
	if (!e || !ISERR(e))
		expect_semi(c, "in 'for' statement specifier");
	skip_close(c, -1);
	accept(c, T_SEMI);
}

/* one declaration or statement of a block (at loc), appended to items */
static void block_item(cc *c, uint32_t loc, vec *items)
{
	if (is(c, T_EOF)) {
		report(c, "error", pp_main_eof(c->p), NULL, 0, NULL, 0, "expected '}'");
		note(c, loc, "to match this '{'");
		longjmp(c->bail, 1);
	}
	if (iskw(c, KW___extension__) && is_decl_start(c, peek(c, 1)))
		next(c);
	if (iskw(c, KW___attribute__) || iskw(c, KW___attribute) || at_std_attr(c)) {
		attrs a = { 0 };

		attribute_list(c, &a);
		if (accept(c, T_SEMI))
			return;	/* __attribute__((fallthrough)); */
		if (is_decl_start(c, tok(c))) {
			c->pend = a;
			c->has_pend = true;
			declaration(c, items, false);
			return;
		}
	}
	if (is_decl_start(c, tok(c)) && !(is(c, T_IDENT) && peek(c, 1)->kind == T_COLON)) {
		declaration(c, items, false);
		return;
	}
	if (is(c, T_IDENT) && !kwof(tok(c)) && stmt_kw_typo(c, true) && is_decl_start(c, tok(c))) {
		declaration(c, items, false);
		return;
	}
	if (is(c, T_IDENT) && !kwof(tok(c)) && (peek(c, 1)->kind == T_IDENT || peek(c, 1)->kind == T_STAR) &&
	    undeclared_type_decl(c, items))
		return;
	vpush(c, items, statement(c));
}

VPRIV uint32_t compound_stmt(cc *c, qty *last)
{
	uint32_t loc = tok(c)->loc, n;
	vec items, locals;

	vinit(&items);
	vinit(&locals);
	expect(c, T_LBRACE, "'{'");
	scope_open(c);
	/* __label__ a, b; */
	while (iskw(c, KW___label__)) {
		next(c);
		do {
			uint32_t l;

			if (!is(c, T_IDENT))
				fatal(c, tok(c)->loc, "expected identifier");
			l = c->nlabel++;
			memset(&c->labels[l], 0, sizeof(label));
			c->labels[l].name = tok(c)->id;
			c->labels[l].loc = tok(c)->loc;
			c->labels[l].local = 1;
			c->labels[l].shadow = dget(c->ldir, tok(c)->id);
			*dslot(c, c->ldir, tok(c)->id) = l;
			vpush(c, &locals, l);
			next(c);
		} while (accept(c, T_COMMA));
		expect_semi(c, "at end of declaration");
	}
	{
		recov r;

		if (REC_ARM(c, &r))
			rec_restore(c, &r, REC_BLOCK);	/* after a parse error: on with the next statement */
		while (!is(c, T_RBRACE))
			block_item(c, loc, &items);
		REC_POP(c, &r);
	}
	next(c);
	if (last) {
		*last = prim(TY_VOID);
		uint32_t ls = items.n ? items.v[items.n - 1] : 0;

		while (ls && N(ls)->op == N_LABEL)	/* clang: a labeled statement gives its value */
			ls = N(ls)->c;
		if (ls && N(ls)->op == N_EXPR && N(ls)->a) {
			uint32_t e = decay(c, N(ls)->a);

			*last = QUNQ(N(e)->ty);
		}
	}
	for (uint32_t i = locals.n; i-- > 0;) {
		label *l = &c->labels[locals.v[i]];

		*dslot(c, c->ldir, l->name) = l->shadow;
	}
	scope_close(c);
	n = mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, &items), items.n);
	return n;
}

/* the body of an if, switch or loop: a block of its own (C99 6.8.4p3, 6.8.5p5) */
static uint32_t sub_statement(cc *c)
{
	uint32_t n;

	scope_open(c);
	n = statement(c);
	scope_close(c);
	return n;
}

VPRIV uint32_t statement(cc *c)
{
	ptok *t = tok(c);
	uint32_t loc = t->loc;
	int kw = kwof(t);

	switch (t->kind) {
	case T_LBRACE:
		return compound_stmt(c, NULL);
	case T_LSQUARE:
		if (at_std_attr(c)) {	/* [[fallthrough]]; and friends: no effect on the statement */
			attrs a = { 0 };

			attribute_list(c, &a);
			return statement(c);
		}
		break;
	case T_SEMI:
		next(c);
		return mkstmt(c, N_EXPR, loc, 0, 0, 0);
	case T_IDENT:
		if (!kw && peek(c, 1)->kind == T_COLON) {
			uint32_t l = label_ref(c, t->id, loc), n, s;
			attrs la = { 0 };

			next(c), next(c);
			attribute_list(c, &la);
			if (c->labels[l].defined)
				cc_error(c, loc, "redefinition of label '%.*s'", IDF(c->labels[l].name));
			if (is(c, T_RBRACE))	/* label at end of compound statement (C23) */
				s = mkstmt(c, N_EXPR, loc, 0, 0, 0);
			else if (is_decl_start(c, tok(c)) && !(is(c, T_IDENT) && peek(c, 1)->kind == T_COLON)) {
				vec items;

				vinit(&items);
				declaration(c, &items, false);
				s = mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, &items), items.n);
				/* the declaration stays in the enclosing scope: (approximation) */
			} else
				s = statement(c);
			n = mkstmt(c, N_LABEL, loc, l, 0, s);
			c->labels[l].defined = n;
			return n;
		}
		break;
	default:
		break;
	}
	switch (kw) {
	case KW_if: {
		uint32_t cnd, th, el = 0;

		next(c);
		scope_open(c);	/* C99 6.8.4p3: a selection statement is a block, and so is each substatement */
		cnd = cond_of(c, "if");
		{
			/* clang: runtime-behaviour warnings only in reachable code */
			cval cv;
			int k = cc_eval(c, cnd, &cv) && cv.kind == CV_INT ? cv.i != 0 : -1;

			c->dead += k == 0;
			th = sub_statement(c);
			c->dead -= k == 0;
			if (iskw(c, KW_else)) {
				next(c);
				c->dead += k == 1;
				el = sub_statement(c);
				c->dead -= k == 1;
			}
		}
		scope_close(c);
		return mkstmt(c, N_IF, loc, cnd, th, el);
	}
	case KW_while: {
		uint32_t cnd, body;

		next(c);
		scope_open(c);
		cnd = cond_of(c, "while");
		c->in_loop++;
		body = sub_statement(c);
		c->in_loop--;
		scope_close(c);
		return mkstmt(c, N_WHILE, loc, cnd, body, 0);
	}
	case KW_do: {
		uint32_t cnd, body;

		next(c);
		scope_open(c);
		c->in_loop++;
		body = sub_statement(c);
		c->in_loop--;
		if (!iskw(c, KW_while))
			fatal(c, tok(c)->loc, "expected 'while' in do/while loop");
		next(c);
		cnd = cond_of(c, "while");
		scope_close(c);
		expect_semi(c, "after do/while statement");
		return mkstmt(c, N_DO, loc, cnd, body, 0);
	}
	case KW_for: {
		uint32_t init = 0, cnd = 0, step = 0, body, n;
		vec sb;
		recov r;
		uint32_t open;

		next(c);
		open = tok(c)->loc;
		if (unlikely(!is(c, T_LPAREN)))
			lparen_fail(c, "for");
		next(c);
		scope_open(c);
		if (REC_ARM(c, &r)) {	/* an error in the header: on with the body */
			rec_restore(c, &r, REC_FOR);
			init = cnd = step = 0;
			goto for_body;
		}
		c->for_head = true;
		if (is_decl_start(c, tok(c))) {
			vec items;

			vinit(&items);
			declaration(c, &items, false);
			init = mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, &items), items.n);
		} else {
			uint32_t e = 0;

			if (!is(c, T_SEMI))
				init = mkstmt(c, N_EXPR, loc, e = expr(c), 0, 0), discard(c, e);
			for_semi(c, e);
		}
		if (!is(c, T_SEMI)) {
			cnd = decay(c, expr(c));
			assign_cond_warn(c, cnd);
			cnd = scalar_cond(c, cnd, "for");
		}
		for_semi(c, cnd);
		c->for_head = false;
		if (!is(c, T_RPAREN))
			step = expr(c), discard(c, step);
		expect_close(c, T_RPAREN, open, step);
	for_body:
		REC_POP(c, &r);
		c->in_loop++;
		body = sub_statement(c);
		c->in_loop--;
		scope_close(c);
		vinit(&sb);
		vpush(c, &sb, step);
		vpush(c, &sb, body);
		n = mkstmt(c, N_FOR, loc, init, cnd, vfreeze(c, &sb));
		return n;
	}
	case KW_switch: {
		uint32_t cnd, body, open;
		qty et;
		qty saved = c->switch_ty;

		next(c);
		scope_open(c);
		open = tok(c)->loc;
		if (unlikely(!is(c, T_LPAREN)))
			lparen_fail(c, "switch");
		next(c);
		cnd = expr(c);
		et = N(cnd)->ty;
		while (N(cnd)->op == N_CAST && N(cnd)->a && !c->span[cnd].e && KIND(c, N(N(cnd)->a)->ty) == TY_ENUM)
			cnd = N(cnd)->a, et = N(cnd)->ty;	/* -Wswitch looks at the enum before promotion */
		cnd = rvalue(c, cnd);
		expect_close(c, T_RPAREN, open, cnd);
		if (!is_integer(c, N(cnd)->ty)) {
			pp_span r = rng(c, cnd);

			report(c, "error", loc, &r, 1, NULL, 0, "statement requires expression of integer type ('%s' invalid)",
			       tyname(c, N(cnd)->ty));
		}
		c->switch_ty = N(cnd)->ty;
		c->in_switch++;
		{
			uint32_t sb = c->case_base;
			bool sd = c->sw_default;

			c->case_base = c->ncase;
			c->sw_default = false;
			body = sub_statement(c);
			if (!c->sw_default && KIND(c, et) == TY_ENUM)
				switch_enum_warn(c, et, cnd);
			c->ncase = c->case_base, c->case_base = sb;
			c->sw_default = sd;
		}
		c->in_switch--;
		c->switch_ty = saved;
		scope_close(c);
		return mkstmt(c, N_SWITCH, loc, cnd, body, 0);
	}
	case KW_case: {
		i128 lo, hi;
		uint32_t s, n;

		uint32_t vloc;

		next(c);
		vloc = tok(c)->loc;
		if (!c->in_switch)
			cc_error(c, loc, "'case' statement not in switch statement");
		const_expr(c, &lo, NULL);
		hi = lo;
		if (accept(c, T_ELLIPSIS))
			const_expr(c, &hi, NULL);
		expect(c, T_COLON, "':'");
		if (c->switch_ty)
			lo = trunc_to(c, lo, c->switch_ty), hi = trunc_to(c, hi, c->switch_ty);
		if (c->in_switch) {
			for (uint32_t i = c->case_base; i < c->ncase; i++)
				if (lo <= c->cases[i].hi && c->cases[i].lo <= hi) {
					char b[48];
					i128 d = lo > c->cases[i].lo ? lo : c->cases[i].lo;

					snprintf(b, sizeof(b), "%lld", (long long)d);
					report(c, "error", vloc, NULL, 0, NULL, 0, lo == hi && c->cases[i].lo == c->cases[i].hi
					       ? "duplicate case value '%s'" : "duplicate case value '%s' in case range", b);
					note(c, c->cases[i].loc, "previous case defined here");
					break;
				}
			if (c->ncase == c->capcase) {
				c->capcase = c->capcase ? c->capcase * 2 : 64;
				c->cases = realloc(c->cases, c->capcase * sizeof(*c->cases));
			}
			c->cases[c->ncase++] = (struct swcase){ lo, hi, vloc };
		}
		if (is(c, T_RBRACE))
			s = mkstmt(c, N_EXPR, loc, 0, 0, 0);
		else
			s = statement(c);
		c->ipool[c->nipool] = lo;
		c->ipool[c->nipool + 1] = hi;
		n = mkstmt(c, N_CASE, loc, c->nipool, c->nipool + 1, s);
		c->nipool += 2;
		return n;
	}
	case KW_default: {
		uint32_t s;

		next(c);
		expect(c, T_COLON, "':'");
		if (!c->in_switch)
			cc_error(c, loc, "'default' statement not in switch statement");
		c->sw_default = true;
		s = is(c, T_RBRACE) ? mkstmt(c, N_EXPR, loc, 0, 0, 0) : statement(c);
		return mkstmt(c, N_DEFAULT, loc, 0, 0, s);
	}
	case KW_break:
		next(c);
		if (!c->in_loop && !c->in_switch)
			cc_error(c, loc, "'break' statement not in loop or switch statement");
		expect_semi(c, "after break statement");
		return mkstmt(c, N_BREAK, loc, 0, 0, 0);
	case KW_continue:
		next(c);
		if (!c->in_loop)
			cc_error(c, loc, "'continue' statement not in loop statement");
		expect_semi(c, "after continue statement");
		return mkstmt(c, N_CONTINUE, loc, 0, 0, 0);
	case KW_return: {
		uint32_t e = 0;
		qty rt = c->fn ? T(c, c->syms[c->fn].ty)->base : prim(TY_INT);

		next(c);
		if (!is(c, T_SEMI)) {
			e = decay(c, expr(c));
			if (!is_void(c, rt) && !is_void(c, N(e)->ty))
				e = is_record(c, rt) ? e : convert_as(c, e, rt, CX_RETURN, loc);
			else if (is_void(c, rt) && !is_void(c, N(e)->ty) && c->fn && !(N(e)->flags & NF_ERR)) {
				pp_span r = rng(c, e);

				report(c, "error", loc, &r, 1, NULL, 0, "void function '%.*s' should not return a value [-Wreturn-mismatch]",
				       IDF(c->syms[c->fn].name));
			}
		} else if (c->fn && !is_void(c, rt) && !c->o->implicit_int) {
			report(c, "error", loc, NULL, 0, NULL, 0, "non-void function '%.*s' should return a value [-Wreturn-mismatch]",
			       IDF(c->syms[c->fn].name));
		}
		if (unlikely(e && ISERR(e)) && !is(c, T_SEMI))
			skip_semi(c);	/* clang: after an invalid expression, silently */
		else
			expect_semi(c, "after return statement");
		return mkstmt(c, N_RETURN, loc, e, 0, 0);
	}
	case KW_goto:
		next(c);
		if (accept(c, T_STAR)) {
			uint32_t e = decay(c, expr(c));

			expect_semi(c, "after goto statement");
			return mkstmt(c, N_GOTO_IND, loc, e, 0, 0);
		}
		if (!is(c, T_IDENT))
			fatal(c, tok(c)->loc, "expected identifier");
		{
			uint32_t l = label_ref(c, tok(c)->id, tok(c)->loc);

			next(c);
			expect_semi(c, "after goto statement");
			return mkstmt(c, N_GOTO, loc, l, 0, 0);
		}
	case KW_asm: case KW___asm__: case KW___asm:
		return asm_stmt(c, false);
	case KW___attribute__: case KW___attribute: {
		attrs a = { 0 };

		attribute_list(c, &a);
		if (accept(c, T_SEMI))
			return mkstmt(c, N_EXPR, loc, 0, 0, 0);	/* fallthrough etc. */
		return statement(c);
	}
	default:
		break;
	}
	if (stmt_kw_typo(c, false))
		return statement(c);
	if (is(c, T_IDENT) && !kwof(tok(c)) && peek(c, 1)->kind == T_IDENT && !kwof(peek(c, 1)) &&
	    !lookup(c, tok(c)->id)) {
		/* "foo_t x;" with foo_t unknown: clang reports the identifier and skips the statement */
		undeclared(c, "use of undeclared identifier", tok(c)->id, tok(c)->loc, false, 0, NULL);
		while (!is(c, T_SEMI) && !is(c, T_RBRACE) && !is(c, T_EOF))
			next(c);
		accept(c, T_SEMI);
		return mkstmt(c, N_EXPR, loc, 0, 0, 0);
	}
	{
		uint32_t e = expr(c);

		if (N(e)->op == N_BINARY && is_cmp(N(e)->aux) && !c->in_stmtexpr &&
		    !(N(e)->flags & (NF_PAREN | NF_MACRO | NF_ERR))) {
			int k = N(e)->aux;
			uint32_t op = N(e)->loc;
			pp_span r = { rng(c, N(e)->a).b, rng(c, N(e)->b).e };
			bool eq = k == T_EQUALEQUAL || k == T_EXCLAIMEQ;

			if (cc_warnx(c, "unused-comparison", true, w_all, op, &r, 1, NULL, 0, "%s comparison result unused",
				  eq ? "equality" : "relational") && k == T_EQUALEQUAL) {
				pp_span orr = { op, op + 2 };
				pp_fixit fx = { op, op + 2, "=" };
				pp_dinfo d = { "note", op, &orr, 1, &fx, 1, NULL, 0 };

				pp_diagx(c->p, &d, "use '=' to turn this equality comparison into an assignment");
			}
		}
		if (unlikely(!is(c, T_SEMI)) && ISERR(e))
			skip_semi(c);	/* clang: after an invalid expression, silently to the ';' */
		else
			expect_semi(c, "after expression");
		/* (the last statement of ({ ... }) gives its value) */
		if (!c->in_stmtexpr || !is(c, T_RBRACE) || peek(c, 1)->kind != T_RPAREN)
			discard(c, e);
		return mkstmt(c, N_EXPR, loc, e, 0, 0);
	}
}
