/* vcc front end: statements (asm statements included) */
#include "ccimpl.h"
#include <ctype.h>

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
/*
 * -Wunused-result for the value e that is dropped: a call of a
 * warn_unused_result/nodiscard function; a statement expression: its last
 * expression statement (last: the whole value is dropped), else the others
 */
VPRIV void unused_result(cc *c, uint32_t e, bool last)
{
	if (N(e)->op == N_STMTEXPR) {
		uint32_t b = N(e)->a, n = N(b)->c;

		for (uint32_t i = 0; i < n; i++) {
			uint32_t s = c->lists[N(b)->b + i];

			if (N(s)->op == N_EXPR && N(s)->a && (i + 1 < n ? !last : last))
				unused_result(c, N(s)->a, true);
		}
		return;
	}
	if (N(e)->op == N_CALL) {
		uint32_t f = N(e)->a;

		while (N(f)->op == N_CAST && N(f)->a)
			f = N(f)->a;
		if (N(f)->op == N_VAR && (c->syms[N(f)->a].flags & SF_WUR)) {
			pp_span r = rng(c, e);
			const attrs *a = &c->attr[c->syms[N(f)->a].attrs];

			cc_warnx(c, "unused-result", true, NULL, rng(c, N(e)->a).b, &r, 1, NULL, 0,
				 "ignoring return value of function declared with '%s' attribute",
				 a->wur == 2 ? "nodiscard" : "warn_unused_result");
		}
	}
}

/* clang's Expr::isUnusedResultAWarning: does dropping e's value warn, where */
typedef struct uvw { uint32_t loc; pp_span r[2]; int nr; uint32_t e; } uvw;

static bool vol(cc *c, uint32_t e)
{
	return (QQ(N(e)->ty) & Q_VOLATILE) != 0;
}

static bool uv_warn(cc *c, uint32_t e, uvw *w)
{
	const node *x;

	for (;;) {
		x = N(e);
		if (x->op == N_CAST && !c->span[e].e && x->a) {	/* implicit */
			if (vol(c, x->a) && (N(x->a)->flags & NF_LVALUE))
				return false;	/* a volatile load */
			e = x->a;
			continue;
		}
		break;
	}
	w->e = e, w->nr = 0;
	switch (x->op) {
	case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC: case N_ASSIGN: case N_CALL: case N_BUILTIN:
	case N_VAARG: case N_ZERO: case N_NONE:
		return false;
	case N_UNARY:
		if (x->aux == T_PLUS || x->aux == T_MINUS || x->aux == T_TILDE || x->aux == T_EXCLAIM)
			goto unop;
		return false;
	case N_DEREF:
		if (vol(c, e))
			return false;
		/* fallthrough */
	case N_ADDR:
	unop:
		w->loc = x->loc, w->r[0] = rng(c, x->a), w->nr = 1;
		return true;
	case N_COMMA: {
		uint32_t r = x->b;

		while (N(r)->op == N_CAST && !c->span[r].e && N(r)->a)
			r = N(r)->a;
		if (N(r)->op == N_NUM && c->ipool[N(r)->a] == 0)
			return false;	/* ((x = y), 0): the idiom */
		return uv_warn(c, x->b, w);
	}
	case N_LOGAND: case N_LOGOR: {
		uvw t;

		if (!uv_warn(c, x->b, &t))
			return false;
		w->loc = x->loc, w->r[0] = rng(c, x->a), w->r[1] = rng(c, x->b), w->nr = 2;
		return true;
	}
	case N_BINARY:
		w->loc = x->loc, w->r[0] = rng(c, x->a), w->r[1] = rng(c, x->b), w->nr = 2;
		return true;
	case N_COND: {
		uvw t;

		if (x->b && !uv_warn(c, x->b, &t))
			return false;
		return uv_warn(c, x->c, w);
	}
	case N_MEMBER:
		if (vol(c, e))
			return false;
		w->loc = c->span[e].e ? c->span[e].e : x->loc, w->r[0] = rng(c, x->a), w->nr = 1;	/* the member's name */
		return true;
	case N_INDEX:
		if (vol(c, e))
			return false;
		w->loc = c->span[e].e ? c->span[e].e : x->loc, w->r[0] = rng(c, x->a), w->r[1] = rng(c, x->b), w->nr = 2;
		return true;
	case N_VAR:
		if (vol(c, e))
			return false;	/* a volatile load */
		goto plain;
	case N_STMTEXPR: {
		uint32_t b = x->a, n = N(b)->c, ls = n ? c->lists[N(b)->b + n - 1] : 0;

		while (ls && N(ls)->op == N_LABEL)
			ls = N(ls)->c;
		if (ls && N(ls)->op == N_EXPR && N(ls)->a)
			return uv_warn(c, N(ls)->a, w);
		if (is_void(c, x->ty))
			return false;
		w->loc = x->loc, w->r[0] = rng(c, e), w->nr = 1;
		return true;
	}
	case N_CAST:	/* explicit */
		if (x->aux == CK_VOID)
			return false;
		w->loc = c->span[e].b, w->r[0] = rng(c, x->a), w->nr = 1;
		return true;
	default:
	plain:
		w->loc = rng(c, e).b, w->r[0] = rng(c, e), w->nr = 1;
		return true;
	}
}

/* -Wunused-comparison: e, a dropped comparison (not parenthesized, not from a macro); true if it is one */
VPRIV bool unused_cmp(cc *c, uint32_t e)
{
	if (N(e)->op != N_BINARY || !is_cmp(N(e)->aux) || (N(e)->flags & (NF_PAREN | NF_MACRO | NF_ERR)))
		return false;
	{
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
	return true;
}

/* clang's MaybeDecrementCount: a discarded full expression that only assigns a local variable */
VPRIV void set_only(cc *c, uint32_t e)
{
	const node *x = N(e);
	uint32_t v;
	bool comp;

	if (x->op == N_ASSIGN)
		v = x->a, comp = x->aux != 0;
	else if (x->op == N_PREINC || x->op == N_PREDEC || x->op == N_POSTINC || x->op == N_POSTDEC)
		v = x->a, comp = true;
	else
		return;
	if (N(v)->op != N_VAR || (N(v)->flags & NF_PAREN) || (comp && (QQ(N(v)->ty) & Q_VOLATILE)))
		return;
	if (c->syms[N(v)->a].nref && c->syms[N(v)->a].nref != 0xffff)
		c->syms[N(v)->a].nref--;
}

/* -Wunused-value: e's value is dropped (a statement; comma: the left operand of a comma) */
VPRIV void unused_value(cc *c, uint32_t e, bool comma)
{
	static const char *const g[] = { "unused", "all", "most", NULL };
	uvw w;
	const char *ms = NULL;
	uint32_t ml = 0;

	if (!c->fn || !e || (N(e)->flags & NF_ERR))
		return;
	if (comma && N(e)->op == N_BINARY && is_cmp(N(e)->aux) && !(N(e)->flags & (NF_PAREN | NF_MACRO)) &&
	    (c->in_stmtexpr || unused_cmp(c, e)))
		return;	/* a comparison: -Wunused-comparison instead */
	if (c->dead || !uv_warn(c, e, &w))
		return;
	if (N(e)->op == N_STMTEXPR && (N(e)->flags & NF_MACRO))
		return;	/* clang: a statement expression from a macro */
	if (N(w.e)->op == N_VAR)
		ms = idstr_(c->syms[N(w.e)->a].name), ml = (uint32_t)idlen_(c->syms[N(w.e)->a].name);
	rt_defer(c, "unused-value", g, w.loc, w.r, w.nr, ms, ml, "%s",
		 comma ? "left operand of comma operator has no effect" : "expression result unused");
}

/* clang's printing of a compound literal's initializer list from its source "(type){...}":
   tokens joined, ", " after commas, spaces around binary operators, none inside braces */
static size_t complit_text(const char *t, size_t len, char *buf, size_t n)
{
	size_t i = 0, k = 0;
	int depth = 0;
	char prev = 0;	/* 'v' after an operand, 'o' after an operator or '{' '(' ',' */

	if (!t)
		return 0;
	/* skip "(type)" */
	for (; i < len; i++) {
		if (t[i] == '(')
			depth++;
		else if (t[i] == ')' && --depth == 0) {
			i++;
			break;
		}
	}
	while (i < len && t[i] != '{')
		i++;
	while (i < len && k + 4 < n) {
		char ch = t[i];

		if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
			i++;
			continue;
		}
		if (ch == '"' || ch == '\'') {
			size_t s0 = i++;

			while (i < len && t[i] != ch)
				i += t[i] == '\\' ? 2 : 1;
			i++;
			if (i > len)
				i = len;
			if (k + (i - s0) + 2 >= n)
				break;
			memcpy(buf + k, t + s0, i - s0), k += i - s0;
			prev = 'v';
			continue;
		}
		if (isalnum((unsigned char)ch) || ch == '_' || ch == '.') {
			while (i < len && k + 2 < n && (isalnum((unsigned char)t[i]) || t[i] == '_' || t[i] == '.'))
				buf[k++] = t[i++];
			prev = 'v';
			continue;
		}
		if (ch == ',') {
			buf[k++] = ',', buf[k++] = ' ', i++, prev = 'o';
			continue;
		}
		if (ch == '{' || ch == '(' || ch == '[') {
			buf[k++] = ch, i++, prev = 'o';
			continue;
		}
		if (ch == '}' || ch == ')' || ch == ']') {
			buf[k++] = ch, i++, prev = 'v';
			continue;
		}
		{
			/* an operator: up to 3 characters */
			size_t m = 1;

			while (m < 3 && i + m < len && strchr("+-*/%<>=!&|^", t[i + m]) && strchr("<>=!&|+-", t[i + m - 1]))
				m++;
			if (prev == 'v') {	/* binary */
				buf[k++] = ' ';
				memcpy(buf + k, t + i, m), k += m;
				buf[k++] = ' ';
			} else {
				memcpy(buf + k, t + i, m), k += m;
			}
			i += m, prev = 'o';
		}
	}
	return k;
}

/* clang's -Wreturn-stack-address: the address of a local or parameter returned (addr: e's address is taken) */
static void return_stack_addr(cc *c, uint32_t e, bool addr)
{
	while (e) {
		const node *x = N(e);

		switch (x->op) {
		case N_CAST:
			if (x->aux == CK_DECAY) {
				e = x->a, addr = true;	/* an array's address */
				continue;
			}
			if (addr && x->aux != CK_NOP)
				return;
			e = x->a;
			continue;
		case N_COND:
			if (x->b)
				return_stack_addr(c, x->b, addr);
			e = x->c;
			continue;
		case N_ADDR:
			if (addr)
				return;
			e = x->a, addr = true;
			continue;
		case N_MEMBER:
			if (!addr)
				return;
			e = x->a;	/* s.m: s's storage */
			continue;
		case N_INDEX:
			if (!addr)
				return;
			e = x->a, addr = false;	/* x[i]: the address x is */
			continue;
		case N_COMPLIT: {
			const sym *y = &c->syms[x->b];
			pp_span r = rng(c, e);
			size_t len = 0;
			const char *t;
			char buf[256];
			size_t k = 0;

			if (!addr || !x->b || y->depth == 0 || y->sc == SC_STATIC)
				return;
			t = pp_src(c->p, r.b, r.e, &len);
			k = complit_text(t, len, buf, sizeof buf);
			cc_warnx(c, "return-stack-address", true, NULL, r.b, &r, 1, NULL, 0,
				 "address of stack memory associated with compound literal '%.*s' returned", (int)k, buf);
			return;
		}
		case N_VAR: {
			const sym *y = &c->syms[x->a];
			pp_span r = rng(c, e);

			if (!addr || y->kind != S_VAR || y->depth == 0 || y->sc == SC_STATIC || y->sc == SC_EXTERN ||
			    (y->flags & SF_GLOBAL))
				return;
			cc_warnx(c, "return-stack-address", true, NULL, r.b, &r, 1, NULL, 0,
				 "address of stack memory associated with %s '%.*s' returned",
				 y->flags & SF_PARAM ? "parameter" : "local variable", IDF(y->name));
			return;
		}
		default:
			return;
		}
	}
}

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

static uint32_t cond_of(cc *c)
{
	uint32_t e;

	expect(c, T_LPAREN, "'('");
	e = decay(c, expr(c));
	assign_cond_warn(c, e);
	expect(c, T_RPAREN, "')'");
	return scalar_cond(c, e, "statement");
}

static uint32_t asm_string(cc *c)
{
	return string_index(c);
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

VPRIV uint32_t compound_stmt(cc *c, qty *last)
{
	uint32_t loc = tok(c)->loc, n, sedone = 0;
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
	while (!is(c, T_RBRACE)) {
		if (last && items.n > sedone) {	/* a statement expression: the one before was not the last */
			uint32_t ls = items.v[items.n - 1];

			while (N(ls)->op == N_LABEL)
				ls = N(ls)->c;
			if (N(ls)->op == N_EXPR && N(ls)->a)
				set_only(c, N(ls)->a), unused_value(c, N(ls)->a, false);
			sedone = items.n;
		}
		if (is(c, T_EOF)) {
			report(c, "error", pp_tok_end(c->p, c->prevloc), NULL, 0, NULL, 0, "expected '}'");
			note(c, loc, "to match this '{'");
			longjmp(c->bail, 1);
		}
		if (iskw(c, KW___extension__) && is_decl_start(c, peek(c, 1)))
			next(c);
		if (iskw(c, KW___attribute__) || iskw(c, KW___attribute) || at_std_attr(c)) {
			attrs a = { 0 };

			attribute_list(c, &a);
			if (accept(c, T_SEMI))
				continue;	/* __attribute__((fallthrough)); */
			if (is_decl_start(c, tok(c))) {
				c->pend = a;
				c->has_pend = true;
				declaration(c, &items, false);
				continue;
			}
		}
		if (is_decl_start(c, tok(c)) && !(is(c, T_IDENT) && peek(c, 1)->kind == T_COLON)) {
			declaration(c, &items, false);
			continue;
		}
		c->se_direct = last != NULL;
		vpush(c, &items, statement(c));
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
	bool sed = c->se_direct;	/* a statement of a statement expression's body: checked there (not the last) */

	c->se_direct = 0;
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
			} else {
				c->se_direct = sed;	/* (a labeled last statement gives the value) */
				s = statement(c);
			}
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
		cnd = cond_of(c);
		{
			/* clang: runtime-behaviour warnings only in reachable code */
			cval cv;
			int k = cc_eval(c, cnd, &cv) && cv.kind == CV_INT ? cv.i != 0 : -1;
			uint32_t rp = c->prevloc, tb = tok(c)->loc, eb, n;

			c->cnd++;
			c->dead += k == 0;
			th = sub_statement(c);
			c->dead -= k == 0;
			setspan(c, th, tb);	/* (for -Wsometimes-uninitialized's notes) */
			if (iskw(c, KW_else)) {
				next(c);
				eb = tok(c)->loc;
				c->dead += k == 1;
				el = sub_statement(c);
				c->dead -= k == 1;
				setspan(c, el, eb);
			}
			c->cnd--;
			scope_close(c);
			n = mkstmt(c, N_IF, loc, cnd, th, el);
			c->span[n] = (pp_span){ loc, rp };
			return n;
		}
	}
	case KW_while: {
		uint32_t cnd, body;

		next(c);
		scope_open(c);
		cnd = cond_of(c);
		c->in_loop++;
		c->cnd++, body = sub_statement(c), c->cnd--;
		c->in_loop--;
		scope_close(c);
		return mkstmt(c, N_WHILE, loc, cnd, body, 0);
	}
	case KW_do: {
		uint32_t cnd, body;

		next(c);
		scope_open(c);
		c->in_loop++;
		c->cnd++, body = sub_statement(c), c->cnd--;
		c->in_loop--;
		if (!iskw(c, KW_while))
			fatal(c, tok(c)->loc, "expected 'while' in do/while loop");
		next(c);
		cnd = cond_of(c);
		scope_close(c);
		expect_semi(c, "after do/while statement");
		return mkstmt(c, N_DO, loc, cnd, body, 0);
	}
	case KW_for: {
		uint32_t init = 0, cnd = 0, step = 0, body, n;
		vec sb;

		next(c);
		expect(c, T_LPAREN, "'('");
		scope_open(c);
		if (is_decl_start(c, tok(c))) {
			vec items;

			vinit(&items);
			declaration(c, &items, false);
			init = mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, &items), items.n);
		} else {
			if (!is(c, T_SEMI))
				init = mkstmt(c, N_EXPR, loc, expr(c), 0, 0);
			expect_semi(c, "in 'for' statement specifier");
		}
		if (!is(c, T_SEMI)) {
			cnd = decay(c, expr(c));
			assign_cond_warn(c, cnd);
			cnd = scalar_cond(c, cnd, "for");
		}
		expect_semi(c, "in 'for' statement specifier");
		if (!is(c, T_RPAREN))
			step = expr(c);
		expect(c, T_RPAREN, "')'");
		c->in_loop++;
		c->cnd++, body = sub_statement(c), c->cnd--;
		c->in_loop--;
		if (init && N(init)->op == N_EXPR && N(init)->a)	/* clang's ActOnForStmt: the first and third clauses */
			set_only(c, N(init)->a), unused_value(c, N(init)->a, false);
		if (step)
			set_only(c, step);
		scope_close(c);
		vinit(&sb);
		vpush(c, &sb, step);
		vpush(c, &sb, body);
		n = mkstmt(c, N_FOR, loc, init, cnd, vfreeze(c, &sb));
		return n;
	}
	case KW_switch: {
		uint32_t cnd, body;
		qty et;
		qty saved = c->switch_ty;

		next(c);
		scope_open(c);
		expect(c, T_LPAREN, "'('");
		cnd = expr(c);
		et = N(cnd)->ty;
		while (N(cnd)->op == N_CAST && N(cnd)->a && !c->span[cnd].e && KIND(c, N(N(cnd)->a)->ty) == TY_ENUM)
			cnd = N(cnd)->a, et = N(cnd)->ty;	/* -Wswitch looks at the enum before promotion */
		cnd = rvalue(c, cnd);
		expect(c, T_RPAREN, "')'");
		if (!is_integer(c, N(cnd)->ty))
			cc_error(c, loc, "statement requires expression of integer type");
		c->switch_ty = N(cnd)->ty;
		c->in_switch++;
		{
			uint32_t sb = c->case_base;
			bool sd = c->sw_default;

			c->case_base = c->ncase;
			c->sw_default = false;
			c->cnd++, body = sub_statement(c), c->cnd--;
			if (!c->sw_default && KIND(c, et) == TY_ENUM)
				switch_enum_warn(c, et, cnd);
			if (!c->sw_default) {
				cval cv;

				if (cc_eval(c, cnd, &cv) && cv.kind == CV_INT) {
					bool hit = false;

					for (uint32_t i = c->case_base; i < c->ncase && !hit; i++)
						hit = cv.i >= c->cases[i].lo && cv.i <= c->cases[i].hi;
					if (!hit) {
						pp_span r = rng(c, cnd);

						report(c, "warning", r.b, NULL, 0, NULL, 0, is_signed(c, N(cnd)->ty) ?
						       "no case matching constant switch condition '%lld'" : "no case matching constant switch condition '%llu'",
						       (long long)cv.i);
					}
				}
			}
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
			else if (!is_void(c, rt) && is_void(c, N(e)->ty) && !(N(e)->flags & NF_ERR)) {
				pp_span r = rng(c, e);
				char f[256];

				snprintf(f, sizeof f, "%s", tyname(c, N(e)->ty));	/* tyname: one buffer */
				report(c, "error", r.b, &r, 1, NULL, 0, "returning '%s' from a function with incompatible result type '%s'", f,
				       tyname(c, rt));
			}
			else if (is_void(c, rt) && !is_void(c, N(e)->ty) && c->fn && !(N(e)->flags & NF_ERR)) {
				pp_span r = rng(c, e);

				report(c, "error", loc, &r, 1, NULL, 0, "void function '%.*s' should not return a value [-Wreturn-mismatch]",
				       IDF(c->syms[c->fn].name));
			}
		} else if (c->fn && !is_void(c, rt) && !c->o->implicit_int) {
			report(c, "error", loc, NULL, 0, NULL, 0, "non-void function '%.*s' should return a value [-Wreturn-mismatch]",
			       IDF(c->syms[c->fn].name));
		}
		expect_semi(c, "after return statement");
		if (e && c->fn)
			return_stack_addr(c, e, false);
		if (c->fn && (c->syms[c->fn].flags & SF_NORETURN))
			cc_warnx(c, "invalid-noreturn", true, NULL, loc, NULL, 0, NULL, 0, "function '%.*s' declared 'noreturn' should not return",
				 IDF(c->syms[c->fn].name));
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
	if (is(c, T_IDENT) && !kwof(tok(c)) && peek(c, 1)->kind == T_IDENT && !kwof(peek(c, 1)) &&
	    !lookup(c, tok(c)->id)) {
		/* "foo_t x;" with foo_t unknown: clang reports the identifier and skips the statement */
		undeclared(c, "use of undeclared identifier", tok(c)->id, tok(c)->loc, false);
		while (!is(c, T_SEMI) && !is(c, T_RBRACE) && !is(c, T_EOF))
			next(c);
		accept(c, T_SEMI);
		return mkstmt(c, N_EXPR, loc, 0, 0, 0);
	}
	{
		uint32_t e = expr(c);

		bool cmp = !c->in_stmtexpr && unused_cmp(c, e);

		if (!c->in_stmtexpr)
			unused_result(c, e, true);
		if (!sed)
			set_only(c, e);
		if (!sed && !cmp && !(c->in_stmtexpr && N(e)->op == N_BINARY && is_cmp(N(e)->aux) &&
				      !(N(e)->flags & (NF_PAREN | NF_MACRO))))
			unused_value(c, e, false);
		expect_semi(c, "after expression");
		return mkstmt(c, N_EXPR, loc, e, 0, 0);
	}
}
