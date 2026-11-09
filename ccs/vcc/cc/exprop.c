/* vcc front end: expressions: unary, cast, binary, conditional, assignment, comma */
#include "ccimpl.h"

/* ---------------- expressions: unary, cast, binary ---------------- */

static uint32_t sizeof_type(cc *c, qty t, uint32_t loc, uint32_t e, pp_span r)
{
	int k = KIND(c, t);

	if (k == TY_ARRAY && (T(c, t)->flags & TF_VLA)) {
		uint32_t n = mk(c, N_SIZEOF_VLA, c->t_size, loc);

		N(n)->a = e;
		N(n)->b = t;
		return n;
	}
	if (k == TY_VOID || k == TY_FUNC)
		return mk_int(c, 1, c->t_size, loc);
	if (!complete(c, t)) {
		report(c, "error", loc, &r, 1, NULL, 0, "invalid application of 'sizeof' to an incomplete type '%s'", tyname(c, t));
		if (is_record(c, t) || k == TY_ENUM)
			note(c, c->tags[T(c, t)->aux].loc, "forward declaration of '%s'", tyname(c, t));
	}
	return mk_int(c, (i128)ty_size(c, t), c->t_size, loc);
}

static uint32_t align_of_expr(cc *c, uint32_t e)
{
	const node *x = N(e);
	uint32_t a = ty_align(c, x->ty);

	if (x->op == N_VAR && c->syms[x->a].align > a)
		a = c->syms[x->a].align;
	if (is_array(c, x->ty)) {
		qty t = x->ty;

		while (is_array(c, t))
			t = T(c, t)->base;
		if (ty_align(c, t) > a)
			a = ty_align(c, t);
	}
	return a;
}

static uint32_t unary(cc *c);

/* the operand of sizeof/_Alignof: "( type-name )" gives UINT32_MAX, *ty and its
   span in *r (if r), anything else the (unevaluated) unary expression; '(' is
   consumed before looking at the type */
static uint32_t sizeof_operand(cc *c, qty *ty, pp_span *r)
{
	uint32_t n;

	if (is(c, T_LPAREN)) {
		uint32_t ploc = tok(c)->loc;

		next(c);
		if (is_type_start(c, tok(c))) {
			*ty = type_name(c);
			expect(c, T_RPAREN, "')'");
			if (r)
				*r = (pp_span){ ploc, pp_tok_end(c->p, c->prevloc) };
			return UINT32_MAX;
		}
		c->unevaluated++;
		n = postfix(c, paren_expr(c, ploc));
		c->unevaluated--;
		return n;
	}
	c->unevaluated++;
	n = unary(c);
	c->unevaluated--;
	return n;
}

static __attribute__((noinline)) uint32_t unary_op(cc *c)
{
	ptok *t = tok(c);
	uint32_t loc = t->loc, n;
	int kw = kwof(t);

	switch (t->kind) {
	case T_PLUSPLUS:
	case T_MINUSMINUS: {
		int op = t->kind == T_PLUSPLUS ? N_PREINC : N_PREDEC;

		next(c);
		n = unary(c);
		if (ISERR(n))
			return n;
		need_lvalue(c, n, t->loc);
		need_scalar(c, n, "increment");
		{
			uint32_t r = mk(c, op, QUNQ(N(n)->ty), loc);

			N(r)->a = n;
			return r;
		}
	}
	case T_AMP:
		next(c);
		n = cast_expr(c);
		if (ISERR(n))
			return n;
		bounds_warn(c, n, true);
		return addr_of(c, n, loc);
	case T_STAR:
		next(c);
		n = cast_expr(c);
		if (KIND(c, N(n)->ty) == TY_FUNC || ISERR(n))
			return n;
		return deref(c, n, loc);
	case T_PLUS:
	case T_MINUS:
	case T_TILDE: {
		int op = t->kind;

		next(c);
		n = cast_expr(c);
		if (ISERR(n))
			return n;
		n = rvalue(c, n);
		if (is_complex(c, N(n)->ty))
			return cplx_unary(c, op, n, loc);
		if (is_vector(c, N(n)->ty) ? op == T_TILDE && !is_integer(c, T(c, N(n)->ty)->base)
		    : !is_arith(c, N(n)->ty) || (op == T_TILDE && !is_integer(c, N(n)->ty))) {
			pp_span r = rng(c, n);

			report(c, "error", loc, &r, 1, NULL, 0, "invalid argument type '%s' to unary expression", tyname(c, N(n)->ty));
			return err_node(c, loc);
		}
		{
			uint32_t r = mk(c, N_UNARY, N(n)->ty, loc);

			N(r)->aux = (uint16_t)op;
			N(r)->a = n;
			return r;
		}
	}
	case T_EXCLAIM: {
		uint32_t r;

		next(c);
		n = cast_expr(c);
		if (ISERR(n))
			return n;
		n = scalar_cond(c, rvalue(c, n), "'!'");
		r = mk(c, N_UNARY, prim(TY_INT), loc);
		N(r)->aux = T_EXCLAIM;
		N(r)->a = n;
		return r;
	}
	case T_AMPAMP: {	/* &&label */
		uint32_t r;

		next(c);
		if (!is(c, T_IDENT))
			fatal(c, tok(c)->loc, "expected identifier");
		r = mk(c, N_LABELADDR, ty_ptr(c, prim(TY_VOID)), loc);
		N(r)->a = label_ref(c, tok(c)->id, tok(c)->loc);
		c->labels[N(r)->a].addr_taken = 1;
		next(c);
		return r;
	}
	default:
		break;
	}
	switch (kw) {
	case KW_sizeof: {
		qty ty;
		pp_span r;

		next(c);
		n = sizeof_operand(c, &ty, &r);
		if (n == UINT32_MAX) {
			if (is(c, T_LBRACE)) {	/* sizeof (T){...} */
				uint32_t init, s, cl;

				init = initializer(c, &ty, !c->fn);
				s = sym_new(c, S_VAR, 0, ty, loc);
				cl = mk2(c, N_COMPLIT, ty, loc, init, s);
				N(cl)->flags = NF_LVALUE;
				n = postfix(c, cl);
				return sizeof_type(c, N(n)->ty, loc, n, rng(c, n));
			}
			return sizeof_type(c, ty, loc, 0, r);
		}
		if (bitfield_of(c, n))
			cc_error(c, loc, "invalid application of 'sizeof' to bit-field");
		return sizeof_type(c, N(n)->ty, loc, n, rng(c, n));
	}
	case KW__Alignof:
	case KW___alignof__:
	case KW___alignof: {
		qty ty;

		next(c);
		n = sizeof_operand(c, &ty, NULL);
		if (n == UINT32_MAX) {
			while (is_array(c, ty))
				ty = T(c, ty)->base;
			return mk_int(c, ty_align(c, ty), c->t_size, loc);
		}
		return mk_int(c, align_of_expr(c, n), c->t_size, loc);
	}
	case KW___extension__:
		next(c);
		return cast_expr(c);
	case KW___real__: case KW___real:
	case KW___imag__: case KW___imag: {
		bool im = kw == KW___imag__ || kw == KW___imag;

		next(c);
		n = cast_expr(c);
		if (ISERR(n))
			return n;
		if (is_complex(c, N(n)->ty))
			return cplx_part(c, n, im);
		n = rvalue(c, n);
		if (!is_arith(c, N(n)->ty))
			cc_error(c, loc, "invalid type '%s' to %s operator", "", im ? "__imag" : "__real");
		if (im)	/* of a real value: 0 (the operand still evaluated) */
			return mk2(c, N_COMMA, QUNQ(N(n)->ty), loc, n, zero_of(c, N(n)->ty, loc));
		return n;
	}
	default:
		return postfix(c, primary(c));
	}
}

static uint32_t unary(cc *c)
{
	const ptok *t = tok(c);
	uint32_t b = t->loc, n;

	switch (t->kind) {
	case T_PLUSPLUS: case T_MINUSMINUS: case T_AMP: case T_STAR: case T_PLUS: case T_MINUS:
	case T_TILDE: case T_EXCLAIM: case T_AMPAMP:
		n = unary_op(c);
		break;
	case T_IDENT:
		switch (kwof(t)) {
		case KW_sizeof: case KW__Alignof: case KW___alignof__: case KW___alignof:
		case KW___extension__: case KW___real__: case KW___real: case KW___imag__: case KW___imag:
			n = unary_op(c);
			break;
		default:
			n = postfix(c, primary(c));
			break;
		}
		break;
	default:
		n = postfix(c, primary(c));
		break;
	}
	setspan(c, n, b);	/* the whole unary expression, for diagnostics */
	return n;
}

static uint32_t explicit_cast(cc *c, uint32_t n, qty to, uint32_t loc)
{
	int k;
	uint32_t r;

	if (ISERR(n))
		return n;
	n = decay(c, n);
	if (is_void(c, to))
		return cast_node(c, n, QUNQ(to), CK_VOID);
	if (KIND(c, to) == TY_UNION && !compatible(c, to, N(n)->ty)) {
		if (!union_member_for(c, to, n)) {
			pp_span sp = rng(c, n);

			report(c, "error", loc, &sp, 1, NULL, 0, "cast to union type from type '%s' not present in union", tyname(c, N(n)->ty));
		}
		return cast_node(c, n, QUNQ(to), CK_UNION);
	}
	if (is_vector(c, to) && is_vector(c, N(n)->ty) && T(c, to)->size == T(c, N(n)->ty)->size)
		return cast_node(c, n, QUNQ(to), CK_NOP);
	if (is_vector(c, to) != is_vector(c, N(n)->ty) && (is_vector(c, to) || is_scalar(c, to))) {
		/* GNU: a vector and a scalar of the same size reinterpret the bits */
		uint32_t from = rvalue(c, n);

		if (ty_size(c, to) != ty_size(c, N(from)->ty) || (!is_vector(c, to) && !is_arith(c, to) && !is_ptr(c, to))) {
			qty vt = is_vector(c, to) ? to : N(n)->ty, st = is_vector(c, to) ? N(n)->ty : to;

			cc_error(c, loc, "invalid conversion between vector type '%s' and %s type '%s' of different size",
				 tyname(c, vt), is_integer(c, st) ? "integer" : "scalar", tyname(c, st));
			return cast_node(c, from, QUNQ(to), CK_NOP);	/* the cast's type: no follow-on errors (no code is generated) */
		}
		return cast_node(c, from, QUNQ(to), is_vector(c, to) ? CK_UNION : CK_VBITS);
	}
	if (is_complex(c, to) || (is_complex(c, N(n)->ty) && is_arith(c, to))) {
		uint32_t r;

		if (!is_arith(c, N(n)->ty) && !is_complex(c, N(n)->ty)) {
			cc_error(c, loc, "used type '%s' where arithmetic or pointer type is required", tyname(c, N(n)->ty));
			return n;
		}
		r = convert(c, rvalue(c, n), QUNQ(to));
		return r;
	}
	if (!is_scalar(c, to) && !compatible(c, to, N(n)->ty)) {
		cc_error(c, loc, "used type '%s' where arithmetic or pointer type is required", tyname(c, N(n)->ty));
		return n;
	}
	if (is_integer(c, N(n)->ty))
		n = rvalue(c, n);
	k = conv_kind(c, N(n)->ty, QUNQ(to), n);
	if (k < 0 && is_scalar(c, to) && !is_scalar(c, N(n)->ty)) {
		pp_span r = rng(c, n);

		report(c, "error", r.b, &r, 1, NULL, 0, "operand of type '%s' where arithmetic or pointer type is required",
		       tyname(c, N(n)->ty));
		n = mk_int(c, 0, QUNQ(to), loc);
		N(n)->flags |= NF_ERR;
		return n;
	}
	if (k < 0) {
		cc_error(c, loc, "invalid cast");
		return n;
	}
	if (k == CK_NULL)
		k = CK_I2P;	/* keep (void *)0 recognisable as a cast */
	r = cast_node(c, n, QUNQ(to), k);
	N(r)->loc = loc;
	return r;
}

/* (type-name) cast-expression or compound literal; '(' (at loc) is consumed */
static __attribute__((noinline)) uint32_t cast_paren(cc *c, uint32_t loc)
{
	{
		uint32_t n;
		qty ty;

		ty = type_name(c);
		expect(c, T_RPAREN, "')'");
		if (is(c, T_LBRACE)) {	/* compound literal */
			uint32_t init, s;
			bool st = !c->fn;

			init = initializer(c, &ty, st);
			s = sym_new(c, S_VAR, 0, ty, loc);
			c->syms[s].body = init;
			c->syms[s].sc = st ? SC_STATIC : SC_AUTO;
			if (st)
				c->syms[s].flags |= SF_GLOBAL | SF_DEFINED;
			n = mk2(c, N_COMPLIT, ty, loc, init, s);
			N(n)->flags = NF_LVALUE;
			return postfix(c, n);
		}
		n = cast_expr(c);
		return explicit_cast(c, n, ty, loc);
	}
}

/* clang: "(doubl)x", a typo of a type keyword in parentheses before an operand,
   is reported with the keyword and parsed as a cast; '(' is consumed */
static bool paren_type_typo(cc *c)
{
	ptok *t = tok(c);
	uint32_t kw;
	int k;

	if (t->kind != T_IDENT || kwof(t) || lookup(c, t->id) || peek(c, 1)->kind != T_RPAREN)
		return false;	/* declared: no peek */
	k = peek(c, 2)->kind;
	if (k != T_IDENT && k != T_NUMBER && k != T_CHAR && k != T_LPAREN)
		return false;
	t = tok(c);
	if (!(kw = typo_keyword(c, t->id, KWS_TYPE)) || !(kwclass[kwmap[kw]] & KC_TYPE))
		return false;
	undeclared(c, "use of undeclared identifier", t->id, t->loc, true, KWS_TYPE, &kw);
	tok(c)->id = kw;
	return true;
}

VPRIV uint32_t cast_expr(cc *c)
{
	uint32_t b = tok(c)->loc, n;

	if (vstack_low())	/* every nested expression comes through here */
		fatal(c, b, "expression nested too deeply");

	if (is(c, T_LPAREN)) {
		/* consumed first: no lookahead token to shift out later */
		next(c);
		if (is_type_start(c, tok(c)) || paren_type_typo(c))
			n = cast_paren(c, b);
		else
			n = postfix(c, paren_expr(c, b));
	} else {
		n = unary(c);
	}
	setspan(c, n, b);	/* the whole cast expression, for diagnostics */
	return n;
}

static int binprec(int k)
{
	switch (k) {
	case T_STAR: case T_SLASH: case T_PERCENT: return 10;
	case T_PLUS: case T_MINUS: return 9;
	case T_LESSLESS: case T_GREATERGREATER: return 8;
	case T_LESS: case T_LESSEQ: case T_GREATER: case T_GREATEREQ: return 7;
	case T_EQUALEQUAL: case T_EXCLAIMEQ: return 6;
	case T_AMP: return 5;
	case T_CARET: return 4;
	case T_PIPE: return 3;
	case T_AMPAMP: return 2;
	case T_PIPEPIPE: return 1;
	default: return 0;
	}
}

/* -Wdivision-by-zero, -Wshift-count-*: l op r with a constant r */
static void runtime_warn(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	cval cv;
	pp_span rr;

	if (c->dead || (op != T_SLASH && op != T_PERCENT && op != T_LESSLESS && op != T_GREATERGREATER) ||
	    !is_integer(c, N(l)->ty) || !is_integer(c, N(r)->ty) || !cc_eval(c, r, &cv) || cv.kind != CV_INT)
		return;
	rr = rng(c, r);
	if (op == T_SLASH || op == T_PERCENT) {
		if (!cv.i)
			cc_warnx(c, "division-by-zero", true, NULL, loc, &rr, 1, NULL, 0, "%s by zero is undefined",
			      op == T_SLASH ? "division" : "remainder");
		return;
	}
	if (cv.i < 0)
		cc_warnx(c, "shift-count-negative", true, NULL, loc, &rr, 1, NULL, 0, "shift count is negative");
	else if (cv.i >= (i128)ty_size(c, promoted(c, l)) * 8)
		cc_warnx(c, "shift-count-overflow", true, NULL, loc, &rr, 1, NULL, 0, "shift count >= width of type");
}

static uint32_t binary(cc *c, int minp);

static __attribute__((noinline)) uint32_t binary_ops(cc *c, int minp, uint32_t l, uint32_t b)
{
	for (;;) {
		int op = tok(c)->kind, p = binprec(op);
		uint32_t loc, r;

		if (!p || p < minp)
			return l;
		bool mac = tok(c)->flags & PF_MACRO;

		loc = tok(c)->loc;
		next(c);
		{
			/* 0 && r, 1 || r: r is not evaluated (no runtime-behaviour warnings) */
			cval cv;
			bool dead = (op == T_AMPAMP || op == T_PIPEPIPE) && cc_eval(c, l, &cv) && cv.kind == CV_INT &&
				    (cv.i != 0) == (op == T_PIPEPIPE);

			c->dead += dead;
			r = binary(c, p + 1);
			c->dead -= dead;
		}
		if (unlikely(ISERR(l) || ISERR(r))) {
			l = err_node(c, loc);
		} else {
			runtime_warn(c, op, l, r, loc);
			l = binop(c, op, l, r, loc);
		}
		if (mac)
			N(l)->flags |= NF_MACRO;
		setspan(c, l, b);
	}
}

static uint32_t binary(cc *c, int minp)
{
	uint32_t b = tok(c)->loc, l = cast_expr(c);
	int p = binprec(tok(c)->kind);

	if (likely(!p || p < minp))
		return l;
	return binary_ops(c, minp, l, b);
}

static __attribute__((noinline)) uint32_t cond_rest(cc *c, uint32_t cnd)
{
	{
		uint32_t loc = tok(c)->loc, a = 0, b;

		cval cv;
		int k;

		next(c);
		cnd = scalar_cond(c, decay(c, cnd), "conditional");
		k = cc_eval(c, cnd, &cv) && cv.kind == CV_INT ? cv.i != 0 : -1;
		c->dead += k == 0;
		if (!is(c, T_COLON))
			a = expr(c);
		c->dead -= k == 0;
		expect(c, T_COLON, "':'");
		c->dead += k == 1;
		b = cond_expr(c);
		c->dead -= k == 1;
		if (ISERR(cnd) || (a && ISERR(a)) || ISERR(b))
			return err_node(c, loc);
		return conditional(c, cnd, a, b, loc);
	}
}

VPRIV uint32_t cond_expr(cc *c)
{
	uint32_t cnd = binary(c, 1);

	if (likely(!is(c, T_QUESTION)))
		return cnd;
	return cond_rest(c, cnd);
}

static int assign_op(int k)
{
	switch (k) {
	case T_EQUAL: return -1;
	case T_STAREQ: return T_STAR;
	case T_SLASHEQ: return T_SLASH;
	case T_PERCENTEQ: return T_PERCENT;
	case T_PLUSEQ: return T_PLUS;
	case T_MINUSEQ: return T_MINUS;
	case T_LESSLESSEQ: return T_LESSLESS;
	case T_GREATERGREATEREQ: return T_GREATERGREATER;
	case T_AMPEQ: return T_AMP;
	case T_CARETEQ: return T_CARET;
	case T_PIPEEQ: return T_PIPE;
	default: return 0;
	}
}

static __attribute__((noinline)) uint32_t assign_rest(cc *c, uint32_t l, int op)
{
	uint32_t loc = tok(c)->loc, r;
	bool mac = tok(c)->flags & PF_MACRO;

	next(c);
	r = assign_expr(c);
	if (ISERR(l) || ISERR(r))
		return err_node(c, loc);
	r = assign(c, op < 0 ? 0 : op, l, r, loc);
	if (mac)
		N(r)->flags |= NF_MACRO;
	return r;
}

VPRIV uint32_t assign_expr(cc *c)
{
	uint32_t l = cond_expr(c);
	int op = assign_op(tok(c)->kind);

	if (likely(!op))
		return l;
	return assign_rest(c, l, op);
}

static __attribute__((noinline)) uint32_t comma_rest(cc *c, uint32_t l)
{
	while (is(c, T_COMMA) && !not_expr_start(c, peek(c, 1))) {
		uint32_t loc = tok(c)->loc, r;

		next(c);
		r = assign_expr(c);
		l = mk2(c, N_COMMA, QUNQ(N(decay(c, r))->ty), loc, l, decay(c, r));
	}
	return l;
}

VPRIV uint32_t expr(cc *c)
{
	uint32_t l = assign_expr(c);

	if (likely(!is(c, T_COMMA)))
		return l;
	return comma_rest(c, l);
}

/* a constant expression as an int (array sizes, widths, enum values, cases) */
VPRIV bool const_expr(cc *c, i128 *v, uint32_t *np)
{
	uint32_t n = rvalue(c, cond_expr(c));

	if (np)
		*np = n;
	if (!const_int(c, n, v)) {
		cc_error(c, N(n)->loc, "expression is not an integer constant expression");
		*v = 0;
		return false;
	}
	return true;
}
