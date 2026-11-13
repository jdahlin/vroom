/* vcc front end: can a function body fall off its end (clang's CheckFallThrough, -Wreturn-type, -Winvalid-noreturn) */
#include "cc-internal.h"

/*
 * Reachability over the statement tree, as clang's CFG sees it: constant
 * conditions prune branches (if (0), while (1), a constant switch), calls
 * of noreturn functions end a path, goto/labels by a fixpoint over the
 * labels (computed goto: every label whose address is taken).
 */

/* a condition: 1 constant true, 0 constant false, -1 not constant */
CC_PRIVATE int cc_reach_condition_constant(cc_reach_state *f, uint32_t n)
{
	cval cv;

	cc *c = f->c;

	if (!n)
		return 1;	/* for (;;) */
	if (cc_eval(c, n, &cv) && cv.kind == CV_INT)
		return cv.i != 0;
	/* clang's tryEvaluateBool: && and || with one known side, !, a const variable's initializer */
	switch (AST_NODE(n)->op) {
	case N_LOGOR: {
		int a = cc_reach_condition_constant(f, AST_NODE(n)->a), b;

		if (a == 1)
			return 1;
		b = cc_reach_condition_constant(f, AST_NODE(n)->b);
		return b == 1 ? 1 : a == 0 && b == 0 ? 0 : -1;
	}
	case N_LOGAND: {
		int a = cc_reach_condition_constant(f, AST_NODE(n)->a), b;

		if (a == 0)
			return 0;
		b = cc_reach_condition_constant(f, AST_NODE(n)->b);
		return b == 0 ? 0 : a == 1 && b == 1 ? 1 : -1;
	}
	case N_UNARY:
		if (AST_NODE(n)->aux == LEX_TOKEN_EXCLAMATION) {
			int a = cc_reach_condition_constant(f, AST_NODE(n)->a);

			return a < 0 ? -1 : !a;
		}
		return -1;
	case N_CAST:
		return AST_NODE(n)->aux == CK_BOOL || AST_NODE(n)->aux == CK_INT || AST_NODE(n)->aux == CK_NOP ? cc_reach_condition_constant(f, AST_NODE(n)->a) : -1;
	case N_VAR: {
		const sym *y = &c->syms[AST_NODE(n)->a];

		if (y->kind == S_VAR && (QQ(y->ty) & Q_CONST) && !(QQ(y->ty) & Q_VOLATILE) && y->body &&
		    cc_type_is_integer(c, y->ty) && cc_eval(c, y->body, &cv) && cv.kind == CV_INT)
			return cv.i != 0;
		return -1;
	}
	default:
		return -1;
	}
}

/* an expression statement that never completes: a call of a noreturn function */
CC_PRIVATE bool cc_reach_is_noreturn(cc_reach_state *f, uint32_t e)
{
	cc *c = f->c;

	while (e && AST_NODE(e)->op == N_CAST)
		e = AST_NODE(e)->a;
	if (!e)
		return false;
	if (AST_NODE(e)->op == N_COMMA)
		return cc_reach_is_noreturn(f, AST_NODE(e)->a) || cc_reach_is_noreturn(f, AST_NODE(e)->b);
	if (AST_NODE(e)->op == N_BUILTIN)
		return AST_NODE(e)->aux == BI_UNREACHABLE || AST_NODE(e)->aux == BI_TRAP;
	if (AST_NODE(e)->op == N_CALL) {
		uint32_t fn = AST_NODE(e)->a;

		while (AST_NODE(fn)->op == N_CAST)
			fn = AST_NODE(fn)->a;
		if (AST_NODE(fn)->op == N_VAR && (c->syms[AST_NODE(fn)->a].flags & SF_NORETURN))
			return true;
		/* a call through a pointer to a noreturn function type is not tracked */
	}
	return false;
}

/* a noreturn call anywhere in the expression (?:, &&, || operands, arguments) */
static bool cc_reach_has_noreturn(cc_reach_state *f, uint32_t e)
{
	cc *c = f->c;
	const node *x;

	if (!e)
		return false;
	x = AST_NODE(e);
	switch (x->op) {
	case N_CALL:
		if (cc_reach_is_noreturn(f, e))
			return true;
		for (uint32_t i = 0; i < x->c; i++)
			if (cc_reach_has_noreturn(f, c->lists[x->b + i]))
				return true;
		return false;
	case N_BUILTIN:
		return cc_reach_is_noreturn(f, e);
	case N_COND:
		return cc_reach_has_noreturn(f, x->a) || cc_reach_has_noreturn(f, x->b) || cc_reach_has_noreturn(f, x->c);
	case N_LOGAND: case N_LOGOR: case N_COMMA: case N_BINARY: case N_ASSIGN:
		return cc_reach_has_noreturn(f, x->a) || cc_reach_has_noreturn(f, x->b);
	case N_CAST: case N_UNARY:
		return cc_reach_has_noreturn(f, x->a);
	default:
		return false;
	}
}

static void cc_reach_label_mark(cc_reach_state *f, uint32_t l)
{
	if (l >= f->first && l - f->first < f->nlab && !f->lab[l - f->first]) {
		f->lab[l - f->first] = 1;
		f->changed = true;
	}
}


/* the switch body's own case labels (not a nested switch's): one matches v / a default */
CC_PRIVATE bool cc_reach_case_matches(cc_reach_state *f, uint32_t n, i128 v)
{
	cc *c = f->c;
	const node *x;

	if (!n)
		return false;
	x = AST_NODE(n);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			if (cc_reach_case_matches(f, c->lists[x->b + i], v))
				return true;
		return false;
	case N_CASE:
		return (v >= c->ipool[x->a] && v <= c->ipool[x->b]) || cc_reach_case_matches(f, x->c, v);
	case N_DEFAULT: case N_LABEL:
		return cc_reach_case_matches(f, x->c, v);
	case N_IF:
		return cc_reach_case_matches(f, x->b, v) || cc_reach_case_matches(f, x->c, v);
	case N_WHILE: case N_DO:
		return cc_reach_case_matches(f, x->b, v);
	case N_FOR:
		return cc_reach_case_matches(f, c->lists[x->c + 1], v);
	default:
		return false;
	}
}

CC_PRIVATE bool cc_reach_has_default(cc_reach_state *f, uint32_t n)
{
	cc *c = f->c;
	const node *x;

	if (!n)
		return false;
	x = AST_NODE(n);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			if (cc_reach_has_default(f, c->lists[x->b + i]))
				return true;
		return false;
	case N_DEFAULT:
		return true;
	case N_CASE: case N_LABEL:
		return cc_reach_has_default(f, x->c);
	case N_IF:
		return cc_reach_has_default(f, x->b) || cc_reach_has_default(f, x->c);
	case N_WHILE: case N_DO:
		return cc_reach_has_default(f, x->b);
	case N_FOR:
		return cc_reach_has_default(f, c->lists[x->c + 1]);
	default:
		return false;
	}
}

static bool cc_reach_statement(cc_reach_state *f, uint32_t n, bool in);

static bool cc_reach_loop_body(cc_reach_state *f, uint32_t body, bool in, bool *brk, bool *cont)
{
	bool sb = f->brk, sc = f->cont, out;

	f->brk = f->cont = false;
	out = cc_reach_statement(f, body, in);
	*brk = f->brk, *cont = f->cont;
	f->brk = sb, f->cont = sc;
	return out;
}

static bool cc_reach_statement(cc_reach_state *f, uint32_t n, bool in)
{
	cc *c = f->c;
	const node *x;

	if (!n)
		return in;
	x = AST_NODE(n);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			in = cc_reach_statement(f, c->lists[x->b + i], in);
		return in;
	case N_EXPR:
		if (in && x->a && AST_NODE(x->a)->op == N_STMTEXPR)
			return cc_reach_statement(f, AST_NODE(x->a)->a, in);
		if (in && cc_reach_is_noreturn(f, x->a)) {
			f->fake = true;
			return false;
		}
		if (in && cc_reach_has_noreturn(f, x->a))
			f->fake = true;	/* (a noreturn call on some path through the expression) */
		return in;
	case N_DECL:
		return in;
	case N_RETURN:
		if (in)
			f->live_ret = true;
		return false;
	case N_IF: {
		int k = cc_reach_condition_constant(f, x->a);
		bool t = cc_reach_statement(f, x->b, in && k != 0), e = cc_reach_statement(f, x->c, in && k != 1);

		return t || e;
	}
	case N_WHILE: {
		int k = cc_reach_condition_constant(f, x->a);
		bool brk, cont, end = cc_reach_loop_body(f, x->b, in && k != 0, &brk, &cont);

		return brk || ((in || end || cont) && k != 1);
	}
	case N_DO: {
		int k = cc_reach_condition_constant(f, x->a);
		bool brk, cont, end = cc_reach_loop_body(f, x->b, in, &brk, &cont);

		return brk || ((end || cont) && k != 1);
	}
	case N_FOR: {
		uint32_t step = c->lists[x->c], body = c->lists[x->c + 1];
		int k;
		bool brk, cont, end;

		in = cc_reach_statement(f, x->a, in);
		k = cc_reach_condition_constant(f, x->b);
		end = cc_reach_loop_body(f, body, in && k != 0, &brk, &cont);
		if (step && AST_NODE(step)->op == N_STMTEXPR) {
			/* a statement expression step (the kernel's scoped_guard: ({ goto l; })) */
			bool back = cc_reach_statement(f, AST_NODE(step)->a, end || cont);

			return brk || ((in || back) && k != 1);
		}
		return brk || ((in || end || cont) && k != 1);
	}
	case N_SWITCH: {
		bool sb = f->brk, sc = f->cont, si = f->sw_in, sk = f->sw_const, sh = f->sw_hit, out, def = false;
		i128 sv = f->sw_val;
		cval cv;

		f->brk = false;
		f->sw_in = in;
		f->sw_const = cc_eval(c, x->a, &cv) && cv.kind == CV_INT;
		f->sw_val = f->sw_const ? cv.i : 0;
		f->sw_hit = f->sw_const && cc_reach_case_matches(f, x->b, f->sw_val);
		def = cc_reach_has_default(f, x->b) || cc_switch_is_enum_covered(c, x);	/* (clang: no edge past it) */
		out = cc_reach_statement(f, x->b, false);
		def = f->sw_const ? (f->sw_hit || def) : def;
		out = out || f->brk || (in && !def);
		f->cont = sc || f->cont;
		f->brk = sb, f->sw_in = si, f->sw_const = sk, f->sw_val = sv, f->sw_hit = sh;
		return out;
	}
	case N_CASE: {
		bool here = f->sw_in;

		if (f->sw_const) {
			i128 lo = c->ipool[x->a], hi = c->ipool[x->b];

			here = here && f->sw_val >= lo && f->sw_val <= hi;
		}
		return cc_reach_statement(f, x->c, in || here);
	}
	case N_DEFAULT: {
		/* a constant switch reaches default only when no case matches */
		bool here = f->sw_in && !(f->sw_const && f->sw_hit);

		return cc_reach_statement(f, x->c, in || here);
	}
	case N_LABEL:
		return cc_reach_statement(f, x->c, in || (x->a >= f->first && x->a - f->first < f->nlab && f->lab[x->a - f->first]));
	case N_GOTO:
		if (in)
			cc_reach_label_mark(f, x->a);
		return false;
	case N_GOTO_IND:
		if (in)
			for (uint32_t l = f->first; l < f->first + f->nlab; l++)
				if (c->labels[l].addr_taken)
					cc_reach_label_mark(f, l);
		return false;
	case N_BREAK:
		if (in)
			f->brk = true;
		return false;
	case N_CONTINUE:
		if (in)
			f->cont = true;
		return false;
	case N_ASM: {
		const casm *a = &c->asms[x->a];

		if (in)
			for (uint32_t i = 0; i < a->nlabel; i++)
				cc_reach_label_mark(f, a->labels[i]);
		return in;
	}
	default:
		return in;
	}
}

/*
 * After a function body: 0 the end is not reached, 1 it may be (clang's
 * MaybeFallThrough: also returns or noreturn calls), 2 it always is.
 */
CC_PRIVATE int cc_reach_body_falls_off(cc *c, uint32_t body, uint32_t first_label)
{
	cc_reach_state f = { .c = c };
	uint8_t small[256];
	bool end = false;
	uint32_t nl = c->nlabel - first_label;

	f.first = first_label, f.nlab = nl;
	f.lab = nl <= sizeof(small) ? small : calloc(nl, 1);
	if (!f.lab)
		return 0;
	if (nl <= sizeof(small))
		memset(small, 0, nl);
	for (int it = 0; it < 16; it++) {
		f.changed = false;
		f.live_ret = f.fake = false;
		f.brk = f.cont = f.sw_in = false;
		end = cc_reach_statement(&f, body, true);
		if (!f.changed)
			break;
	}
	if (f.lab != small)
		free(f.lab);
	if (!end)
		return 0;
	return f.live_ret || f.fake ? 1 : 2;
}
