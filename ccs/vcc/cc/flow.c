/* vcc front end: can a function body fall off its end (clang's CheckFallThrough, -Wreturn-type, -Winvalid-noreturn) */
#include "ccimpl.h"

/*
 * Reachability over the statement tree, as clang's CFG sees it: constant
 * conditions prune branches (if (0), while (1), a constant switch), calls
 * of noreturn functions end a path, goto/labels by a fixpoint over the
 * labels (computed goto: every label whose address is taken).
 */
typedef struct flw {
	cc *c;
	uint8_t *lab;		/* label reached, from first */
	uint32_t first, nlab;
	bool changed;
	bool brk, cont;		/* the innermost loop/switch: reached by break/continue */
	bool sw_in;		/* the innermost switch: its condition reached */
	bool sw_const;		/* ... with a constant condition */
	i128 sw_val;
	bool sw_hit;		/* ... that some case matches */
	bool live_ret, fake;	/* a reachable return; a reachable noreturn call */
} flw;

/* a condition: 1 constant true, 0 constant false, -1 not constant */
static int fl_const(flw *f, uint32_t n)
{
	cval cv;

	cc *c = f->c;

	if (!n)
		return 1;	/* for (;;) */
	if (cc_eval(c, n, &cv) && cv.kind == CV_INT)
		return cv.i != 0;
	/* clang's tryEvaluateBool: && and || with one known side, !, a const variable's initializer */
	switch (N(n)->op) {
	case N_LOGOR: {
		int a = fl_const(f, N(n)->a), b;

		if (a == 1)
			return 1;
		b = fl_const(f, N(n)->b);
		return b == 1 ? 1 : a == 0 && b == 0 ? 0 : -1;
	}
	case N_LOGAND: {
		int a = fl_const(f, N(n)->a), b;

		if (a == 0)
			return 0;
		b = fl_const(f, N(n)->b);
		return b == 0 ? 0 : a == 1 && b == 1 ? 1 : -1;
	}
	case N_UNARY:
		if (N(n)->aux == T_EXCLAIM) {
			int a = fl_const(f, N(n)->a);

			return a < 0 ? -1 : !a;
		}
		return -1;
	case N_CAST:
		return N(n)->aux == CK_BOOL || N(n)->aux == CK_INT || N(n)->aux == CK_NOP ? fl_const(f, N(n)->a) : -1;
	case N_VAR: {
		const sym *y = &c->syms[N(n)->a];

		if (y->kind == S_VAR && (QQ(y->ty) & Q_CONST) && !(QQ(y->ty) & Q_VOLATILE) && y->body &&
		    is_integer(c, y->ty) && cc_eval(c, y->body, &cv) && cv.kind == CV_INT)
			return cv.i != 0;
		return -1;
	}
	default:
		return -1;
	}
}

/* an expression statement that never completes: a call of a noreturn function */
static bool fl_noreturn(flw *f, uint32_t e)
{
	cc *c = f->c;

	while (e && N(e)->op == N_CAST)
		e = N(e)->a;
	if (!e)
		return false;
	if (N(e)->op == N_COMMA)
		return fl_noreturn(f, N(e)->a) || fl_noreturn(f, N(e)->b);
	if (N(e)->op == N_BUILTIN)
		return N(e)->aux == BI_UNREACHABLE || N(e)->aux == BI_TRAP;
	if (N(e)->op == N_CALL) {
		uint32_t fn = N(e)->a;

		while (N(fn)->op == N_CAST)
			fn = N(fn)->a;
		if (N(fn)->op == N_VAR && (c->syms[N(fn)->a].flags & SF_NORETURN))
			return true;
		/* a call through a pointer to a noreturn function type is not tracked */
	}
	return false;
}

/* a noreturn call anywhere in the expression (?:, &&, || operands, arguments) */
static bool fl_has_noreturn(flw *f, uint32_t e)
{
	cc *c = f->c;
	const node *x;

	if (!e)
		return false;
	x = N(e);
	switch (x->op) {
	case N_CALL:
		if (fl_noreturn(f, e))
			return true;
		for (uint32_t i = 0; i < x->c; i++)
			if (fl_has_noreturn(f, c->lists[x->b + i]))
				return true;
		return false;
	case N_BUILTIN:
		return fl_noreturn(f, e);
	case N_COND:
		return fl_has_noreturn(f, x->a) || fl_has_noreturn(f, x->b) || fl_has_noreturn(f, x->c);
	case N_LOGAND: case N_LOGOR: case N_COMMA: case N_BINARY: case N_ASSIGN:
		return fl_has_noreturn(f, x->a) || fl_has_noreturn(f, x->b);
	case N_CAST: case N_UNARY:
		return fl_has_noreturn(f, x->a);
	default:
		return false;
	}
}

static void fl_reach_label(flw *f, uint32_t l)
{
	if (l >= f->first && l - f->first < f->nlab && !f->lab[l - f->first]) {
		f->lab[l - f->first] = 1;
		f->changed = true;
	}
}


/* the switch body's own case labels (not a nested switch's): one matches v / a default */
static bool fl_case_match(flw *f, uint32_t n, i128 v)
{
	cc *c = f->c;
	const node *x;

	if (!n)
		return false;
	x = N(n);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			if (fl_case_match(f, c->lists[x->b + i], v))
				return true;
		return false;
	case N_CASE:
		return (v >= c->ipool[x->a] && v <= c->ipool[x->b]) || fl_case_match(f, x->c, v);
	case N_DEFAULT: case N_LABEL:
		return fl_case_match(f, x->c, v);
	case N_IF:
		return fl_case_match(f, x->b, v) || fl_case_match(f, x->c, v);
	case N_WHILE: case N_DO:
		return fl_case_match(f, x->b, v);
	case N_FOR:
		return fl_case_match(f, c->lists[x->c + 1], v);
	default:
		return false;
	}
}

static bool fl_has_default(flw *f, uint32_t n)
{
	cc *c = f->c;
	const node *x;

	if (!n)
		return false;
	x = N(n);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			if (fl_has_default(f, c->lists[x->b + i]))
				return true;
		return false;
	case N_DEFAULT:
		return true;
	case N_CASE: case N_LABEL:
		return fl_has_default(f, x->c);
	case N_IF:
		return fl_has_default(f, x->b) || fl_has_default(f, x->c);
	case N_WHILE: case N_DO:
		return fl_has_default(f, x->b);
	case N_FOR:
		return fl_has_default(f, c->lists[x->c + 1]);
	default:
		return false;
	}
}

static bool fl_stmt(flw *f, uint32_t n, bool in);

static bool fl_loop_body(flw *f, uint32_t body, bool in, bool *brk, bool *cont)
{
	bool sb = f->brk, sc = f->cont, out;

	f->brk = f->cont = false;
	out = fl_stmt(f, body, in);
	*brk = f->brk, *cont = f->cont;
	f->brk = sb, f->cont = sc;
	return out;
}

static bool fl_stmt(flw *f, uint32_t n, bool in)
{
	cc *c = f->c;
	const node *x;

	if (!n)
		return in;
	x = N(n);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			in = fl_stmt(f, c->lists[x->b + i], in);
		return in;
	case N_EXPR:
		if (in && x->a && N(x->a)->op == N_STMTEXPR)
			return fl_stmt(f, N(x->a)->a, in);
		if (in && fl_noreturn(f, x->a)) {
			f->fake = true;
			return false;
		}
		if (in && fl_has_noreturn(f, x->a))
			f->fake = true;	/* (a noreturn call on some path through the expression) */
		return in;
	case N_DECL:
		return in;
	case N_RETURN:
		if (in)
			f->live_ret = true;
		return false;
	case N_IF: {
		int k = fl_const(f, x->a);
		bool t = fl_stmt(f, x->b, in && k != 0), e = fl_stmt(f, x->c, in && k != 1);

		return t || e;
	}
	case N_WHILE: {
		int k = fl_const(f, x->a);
		bool brk, cont, end = fl_loop_body(f, x->b, in && k != 0, &brk, &cont);

		return brk || ((in || end || cont) && k != 1);
	}
	case N_DO: {
		int k = fl_const(f, x->a);
		bool brk, cont, end = fl_loop_body(f, x->b, in, &brk, &cont);

		return brk || ((end || cont) && k != 1);
	}
	case N_FOR: {
		uint32_t step = c->lists[x->c], body = c->lists[x->c + 1];
		int k;
		bool brk, cont, end;

		in = fl_stmt(f, x->a, in);
		k = fl_const(f, x->b);
		end = fl_loop_body(f, body, in && k != 0, &brk, &cont);
		if (step && N(step)->op == N_STMTEXPR) {
			/* a statement expression step (the kernel's scoped_guard: ({ goto l; })) */
			bool back = fl_stmt(f, N(step)->a, end || cont);

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
		f->sw_hit = f->sw_const && fl_case_match(f, x->b, f->sw_val);
		def = fl_has_default(f, x->b);
		out = fl_stmt(f, x->b, false);
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
		return fl_stmt(f, x->c, in || here);
	}
	case N_DEFAULT: {
		/* a constant switch reaches default only when no case matches */
		bool here = f->sw_in && !(f->sw_const && f->sw_hit);

		return fl_stmt(f, x->c, in || here);
	}
	case N_LABEL:
		return fl_stmt(f, x->c, in || (x->a >= f->first && x->a - f->first < f->nlab && f->lab[x->a - f->first]));
	case N_GOTO:
		if (in)
			fl_reach_label(f, x->a);
		return false;
	case N_GOTO_IND:
		if (in)
			for (uint32_t l = f->first; l < f->first + f->nlab; l++)
				if (c->labels[l].addr_taken)
					fl_reach_label(f, l);
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
				fl_reach_label(f, a->labels[i]);
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
VPRIV int body_falls_off(cc *c, uint32_t body, uint32_t first_label)
{
	flw f = { .c = c };
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
		end = fl_stmt(&f, body, true);
		if (!f.changed)
			break;
	}
	if (f.lab != small)
		free(f.lab);
	if (!end)
		return 0;
	return f.live_ret || f.fake ? 1 : 2;
}
