/* vcc front end: expressions: unary, cast, binary, conditional, assignment, comma */
#include "ccimpl.h"

/* ---------------- expressions: unary, cast, binary ---------------- */

/* r: the operand's range for a diagnostic; e (if any) gives it, else r.e is its last token's start */
static uint32_t sizeof_type(cc *c, qty t, uint32_t loc, uint32_t e, pp_span r)
{
	int k = KIND(c, t);

	if (k == TY_SVE) {
		cc_error(c, loc, "invalid application of 'sizeof' to sizeless type '%s'", tyname(c, t));
		return mk_int(c, 0, c->t_size, loc);
	}
	if (k == TY_ARRAY && (T(c, t)->flags & TF_VLA)) {
		uint32_t n = mk(c, N_SIZEOF_VLA, c->t_size, loc);

		N(n)->a = e;
		N(n)->b = t;
		return n;
	}
	if (k == TY_VOID || k == TY_FUNC) {
		EXT_WARN_MS(c, "sizeof", "pointer-arith", w_ped, loc, "invalid application of 'sizeof' to a %s type", k == TY_VOID ? "void" : "function");
		return mk_int(c, 1, c->t_size, loc);
	}
	if (!complete(c, t)) {
		r = e ? rng(c, e) : (pp_span){ r.b, pp_tok_end(c->p, r.e) };
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

	if (x->op == N_MEMBER)
		return c->tags[x->b].f[x->c].align;
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
static void ovf_warn(cc *c, int op, uint32_t n, uint32_t loc, uint32_t sub0, uint32_t sub1);

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
			expect_match(c, T_RPAREN, ploc);
			if (r)
				*r = (pp_span){ ploc, c->prevloc };	/* resolved by sizeof_type if needed */
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
		/* Parenthesized type-names may begin an lvalue compound literal. */
		n = cast_expr(c);
		if (ISERR(n))
			return n;
		need_lvalue(c, n, t->loc);
		if (is_complex(c, N(n)->ty))
			return cplx_incdec(c, n, op, loc);
		if ((T(c, N(n)->ty)->flags & TF_ATOMIC) && atomic_wide(c, N(n)->ty))
			return atomic_assign_expr(c, n, 0, op == N_PREINC ? T_PLUS : T_MINUS, false, loc);
		need_scalar(c, n, "increment");
		if (is_ptr(c, N(n)->ty) && KIND(c, T(c, N(n)->ty)->base) == TY_SVE)
			cc_error(c, loc, "arithmetic on a pointer to sizeless type '%s'", tyname(c, T(c, N(n)->ty)->base));
		{
			uint32_t r = mk(c, op, QUNQ(ty_unatomic(c, N(n)->ty)), loc);

			N(r)->a = n;
			return r;
		}
	}
	case T_AMP:
		next(c);
		c->after_amp = is(c, T_STAR);
		n = cast_expr(c);
		if (ISERR(n))
			return n;
		bounds_warn(c, n, true);
		return addr_of(c, n, loc);	/* (addr_of: &r of a register variable) */
	case T_STAR: {
		bool amp = c->after_amp;

		c->after_amp = false;
		next(c);
		n = cast_expr(c);
		if (KIND(c, N(n)->ty) == TY_FUNC || ISERR(n))
			return n;
		n = deref(c, n, loc);
		if (!amp && !c->unevaluated && N(n)->op == N_DEREF && KIND(c, N(n)->ty) == TY_VOID) {
			pp_span r = rng(c, N(n)->a);

			cc_warnx(c, "void-ptr-dereference", true, NULL, loc, &r, 1, NULL, 0,
				 "ISO C does not allow indirection on operand of type '%s'", tyname(c, N(N(n)->a)->ty));
		}
		return n;
	}
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
			if (op == T_MINUS) {
				setspan(c, r, loc);
				ovf_warn(c, op, r, loc, n, 0);
			}
			return r;
		}
	}
	case T_EXCLAIM: {
		uint32_t r;

		next(c);
		n = cast_expr(c);
		if (ISERR(n))
			return n;
		if (is_bool_vector(c, N(n)->ty)) {
			r = mk(c, N_UNARY, QUNQ(N(n)->ty), loc);
			N(r)->aux = T_TILDE, N(r)->a = rvalue(c, n);
			return r;
		}
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
		EXT_WARN_MS(c, "&&", "gnu-label-as-value", w_gnu, loc, "use of GNU address-of-label extension");
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
				return sizeof_type(c, N(n)->ty, loc, n, (pp_span){ 0, 0 });
			}
			return sizeof_type(c, ty, loc, 0, r);
		}
		if (bitfield_of(c, n))
			cc_error(c, loc, "invalid application of 'sizeof' to bit-field");
		return sizeof_type(c, N(n)->ty, loc, n, (pp_span){ 0, 0 });
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
			if (KIND(c, ty) == TY_VOID || KIND(c, ty) == TY_FUNC)
				EXT_WARN(c, "pointer-arith", w_ped, loc, "invalid application of 'alignof' to a %s type",
					 KIND(c, ty) == TY_VOID ? "void" : "function");
			if (KIND(c, ty) == TY_SVE)
				cc_error(c, loc, "invalid application of 'alignof' to sizeless type '%s'", tyname(c, ty));
			return mk_int(c, ty_align(c, ty), c->t_size, loc);
		}
		if (KIND(c, N(n)->ty) == TY_SVE)
			cc_error(c, loc, "invalid application of 'alignof' to sizeless type '%s'", tyname(c, N(n)->ty));
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
	if (is_bool_vector(c, to) && is_arith(c, N(n)->ty))
		return convert(c, n, to); /* explicit scalar casts splat the converted boolean */
	if ((is_bool_vector(c, to) || is_bool_vector(c, N(n)->ty)) && !compatible(c, to, N(n)->ty)) {
		cc_error(c, loc, "invalid conversion between vector type '%s' and type '%s'", tyname(c, to), tyname(c, N(n)->ty));
		return cast_node(c, n, QUNQ(to), CK_NOP);
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

/* Evaluate and capture only bounds introduced by this type-name. Typedef
   bounds already have their saved value from the declaration. A synthetic
   one-dimensional char array lets the existing VLA cache retain each bound. */
VPRIV uint32_t capture_vla_bounds(cc *c, qty ty, uint32_t first, uint32_t value, uint32_t loc)
{
	uint32_t effects = 0;

	for (qty t = ty; is_ptr(c, t) || is_array(c, t); t = T(c, t)->base) {
		const ctype *a = T(c, t);
		if (a->kind == TY_ARRAY && (a->flags & TF_VLA) && a->n >= first && a->n) {
			uint32_t bound = a->n;
			qty bt = ty_vla(c, prim(TY_CHAR), bound);
			uint32_t fix = mk(c, N_SIZEOF_VLA, c->t_size, loc);

			N(fix)->a = bound;
			N(fix)->b = bt;
			N(fix)->aux = 1; /* capture the bound before reading the cached size */
			effects = effects ? mk2(c, N_COMMA, c->t_size, loc, effects, fix) : fix;
		}
	}
	if (!effects)
		return value;
	if (N(value)->flags & NF_LVALUE) {
		/* Sequence through the address so a compound literal stays an lvalue,
		   including when a later postfix operator modifies it. */
		qty vt = N(value)->ty, pt = ty_ptr(c, vt);
		uint32_t address = mk2(c, N_ADDR, pt, loc, value, 0);
		uint32_t sequence = mk2(c, N_COMMA, pt, loc, effects, address);
		uint32_t result = mk2(c, N_DEREF, vt, loc, sequence, 0);

		N(result)->flags = N(value)->flags;
		return result;
	}
	return mk2(c, N_COMMA, N(value)->ty, loc, effects, value);
}

/* (type-name) cast-expression or compound literal; '(' (at loc) is consumed */
static __attribute__((noinline)) uint32_t cast_paren(cc *c, uint32_t loc)
{
	{
		uint32_t n, first_bound = c->nnode;
		int e0 = c->errors;
		qty ty;
		bool bad;

		ty = type_name(c);
		bad = c->errors != e0;	/* the type had its error: clang's invalid cast, silent */
		expect_match(c, T_RPAREN, loc);
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
			return postfix(c, capture_vla_bounds(c, ty, first_bound, n, loc));
		}
		{
			uint32_t td = c->tn_tdef, r;

			n = cast_expr(c);
			if (unlikely(bad))
				return err_node(c, loc);
			r = explicit_cast(c, n, ty, loc);
			/* an explicit cast to a typedef in a body: its name on the node's free b (-Wformat) */
			if (td && c->fn && r != n && N(r)->op == N_CAST && !N(r)->b)
				N(r)->b = td;
			return capture_vla_bounds(c, ty, first_bound, r, loc);
		}
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
	if (op == T_SLASH || op == T_PERCENT ? cv.i != 0 : cv.i >= 0 && cv.i < (i128)ty_size(c, promoted(c, l)) * 8)
		return;	/* no warning: no range (pp_tok_end) either */
	rr = rng(c, r);
	if (op == T_SLASH || op == T_PERCENT) {
		if (!cv.i && c->unevaluated)
			;	/* (sizeof, typeof: never runs) */
		else if (!cv.i && c->defer_rt) {
			if (c->ndefer < 8)
				c->defer[c->ndefer].loc = loc, c->defer[c->ndefer].r = rr, c->defer[c->ndefer++].rem = op == T_PERCENT;
		} else if (!cv.i && c->fn)	/* (clang's DiagRuntimeBehavior: at the end of the function) */
			rt_defer(c, "division-by-zero", NULL, loc, &rr, 1, NULL, 0, "%s by zero is undefined",
				 op == T_SLASH ? "division" : "remainder");
		else if (!cv.i)
			cc_warnx(c, "division-by-zero", true, NULL, loc, &rr, 1, NULL, 0, "%s by zero is undefined",
			      op == T_SLASH ? "division" : "remainder");
		return;
	}
	{
		qty pt = promoted(c, l);
		uint64_t w = ty_size(c, pt) * 8;
		vdiag *d = cv.i < 0 ? cc_warn_begin(c, "shift-count-negative", true, NULL, loc, "shift count is negative")
			   : cc_warn_begin(c, "shift-count-overflow", true, NULL, loc, "shift count >= width of type");

		if (!d)
			return;
		pp_diag_label(c->p, d, rr.b, rr.e, NULL);
		if (cv.i < 0)
			pp_diag_help(c->p, d, "a shift by a negative count is undefined");
		else
			pp_diag_help(c->p, d, "'%s' is %llu bits wide: a shift by %lld or more is undefined%s", tyname(c, pt),
				     (unsigned long long)w, (long long)w,
				     w < 64 && cv.i < 64 && op == T_LESSLESS ? "; shift a 64-bit value (1ULL << n)" : "");
		rt_emit(c, d);
	}
}

/* clang's -Wconstant-logical-operand: 'x && 4' where '&' was likely meant (the right
   operand a constant other than 0 and 1, an integer non-bool left one, no enumerator) */
static __attribute__((cold, noinline)) void logic_const_warn(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc, i128 v)
{
	const char *o = op == T_AMPAMP ? "&&" : "||", *bo = op == T_AMPAMP ? "&" : "|";
	pp_span lr = rng(c, l), rr = rng(c, r);
	vdiag *d = cc_warn_begin(c, "constant-logical-operand", true, NULL, loc, "use of logical '%s' with constant operand", o), *n;

	(void)v;
	if (!d)
		return;
	pp_diag_label(c->p, d, rr.b, rr.e, NULL);
	n = pp_diag_note(c->p, d, loc, "use '%s' for a bitwise operation", bo);
	if (n)
		pp_diag_fix(c->p, n, loc, loc + 2, bo, VDA_MAYBE);
	if (op == T_AMPAMP) {
		n = pp_diag_note(c->p, d, loc, "remove constant to silence this warning");
		if (n)
			pp_diag_fix(c->p, n, lr.e, rr.e, "", VDA_MAYBE);
	}
	pp_diag_emit(c->p, d);
}

/* clang's isSameComparisonOperand: the same variable, member chain or subscript once
   parentheses and implicit conversions are gone (an explicit cast is another operand) */
static uint32_t no_implicit(cc *c, uint32_t n)
{
	while (N(n)->op == N_CAST && !c->span[n].e)
		n = N(n)->a;
	return n;
}

/* the same object on both sides (clang's diagnoseTautologicalComparison:
   the same variable, member chain or constant-index element; implicit conversions and parentheses
   aside; nothing from a macro) */
static bool same_ref(cc *c, uint32_t a, uint32_t b, int depth)
{
	while (N(a)->op == N_CAST && !c->span[a].e && N(a)->a)
		a = N(a)->a;
	while (N(b)->op == N_CAST && !c->span[b].e && N(b)->a)
		b = N(b)->a;
	if (N(a)->op != N(b)->op || depth > 16)
		return false;
	switch (N(a)->op) {
	case N_VAR: {
		uint32_t nm = c->syms[N(a)->a].name;

		return N(a)->a == N(b)->a && c->syms[N(a)->a].kind == S_VAR && nm &&
		       pp_spells(c->p, N(a)->loc, idstr_(nm), (uint32_t)idlen_(nm)) &&
		       pp_spells(c->p, N(b)->loc, idstr_(nm), (uint32_t)idlen_(nm));
	}
	case N_MEMBER: {
		uint32_t x = N(a)->a, y = N(b)->a;

		if (N(a)->b != N(b)->b || N(a)->c != N(b)->c)
			return false;
		if (N(x)->op == N_DEREF && N(y)->op == N_DEREF)	/* p->m */
			x = N(x)->a, y = N(y)->a;
		return same_ref(c, x, y, depth + 1);
	}
	case N_INDEX: {
		uint32_t i = N(a)->b, j = N(b)->b;

		while (N(i)->op == N_CAST && !c->span[i].e && N(i)->a)
			i = N(i)->a;
		while (N(j)->op == N_CAST && !c->span[j].e && N(j)->a)
			j = N(j)->a;
		return N(i)->op == N_NUM && N(j)->op == N_NUM && c->ipool[N(i)->a] == c->ipool[N(j)->a] &&
		       same_ref(c, N(a)->a, N(b)->a, depth + 1);
	}
	default:
		return false;
	}
}

/* clang's -Wtautological-compare for x == x and friends (not floating: NaN) */
static __attribute__((cold, noinline)) void self_compare(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	bool t = op == T_EQUALEQUAL || op == T_LESSEQ || op == T_GREATEREQ;
	vdiag *d;

	(void)l, (void)r;
	if (c->dead && c->fn)
		return;
	d = cc_warn_begin(c, "tautological-compare", true, NULL, loc, "self-comparison always evaluates to %s", t ? "true" : "false");
	if (!d)
		return;
	pp_diag_help(c->p, d, "both sides are the same %s: was another one meant?",
		     N(no_implicit(c, l))->op == N_VAR ? "variable" : "object");
	pp_diag_emit(c->p, d);
}

static inline void self_cmp(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	if (is_float(c, N(l)->ty) || is_float(c, N(r)->ty) || !same_ref(c, l, r, 0))
		return;
	self_compare(c, op, l, r, loc);
}

/* an operand as clang prints it in these messages: v, s.f, p->f (appended at b[*k]) */
static void operand_name(cc *c, uint32_t n, char *b, size_t sz, size_t *k)
{
	const node *x = N(n = no_implicit(c, n));
	int l;
	const char *nm;

#define PUT(p, len) do { size_t q_ = (size_t)(len); if (*k + q_ < sz) memcpy(b + *k, (p), q_), *k += q_; } while (0)
	if (x->op == N_VAR) {
		nm = idname(c->syms[x->a].name, &l);
		PUT(nm, l);
	} else if (x->op == N_MEMBER) {
		const field *f = &c->tags[x->b].f[x->c];
		uint32_t base = no_implicit(c, x->a);

		if (N(base)->op == N_DEREF) {
			operand_name(c, N(base)->a, b, sz, k);
			PUT("->", 2);
		} else {
			operand_name(c, base, b, sz, k);
			PUT(".", 1);
		}
		if (f->name) {
			nm = idname(f->name, &l);
			PUT(nm, l);
		}
	}
#undef PUT
	b[*k < sz ? *k : sz - 1] = 0;
}

/* clang's -Wtautological-pointer-compare: an array, a function or &object against null */
static __attribute__((cold, noinline)) void null_compare(cc *c, int op, uint32_t x, uint32_t nul)
{
	uint32_t n = no_implicit(c, x), at;
	const node *y = N(n);
	const char *what;
	bool fn = false, eq = op == T_EQUALEQUAL;
	char nm[160];
	size_t nl = 0;
	pp_span xr, nr = rng(c, nul);
	vdiag *d;

	if (y->op == N_ADDR) {
		uint32_t in = no_implicit(c, y->a);

		if ((N(in)->op != N_VAR && N(in)->op != N_MEMBER) || is_func(c, N(in)->ty) ||
		    (N(in)->op == N_VAR && (c->syms[N(in)->a].flags & SF_WEAK)))
			return;
		what = "address of ", n = in;
	} else if (y->op == N_VAR && is_func(c, y->ty)) {
		if (c->syms[y->a].flags & SF_WEAK)
			return;
		what = "function ", fn = true;
	} else if ((y->op == N_VAR || y->op == N_MEMBER) && is_array(c, y->ty)) {
		if (y->op == N_VAR && (c->syms[y->a].flags & SF_WEAK))
			return;
		what = "array ";
	} else {
		return;
	}
	operand_name(c, n, nm, sizeof nm, &nl);
	if (!nl)
		return;
	xr = rng(c, n), at = N(n)->loc;
	if (N(n)->op == N_MEMBER)	/* clang: at the member's name, after '.' or '->' */
		at += N(no_implicit(c, N(n)->a))->op == N_DEREF ? 2 : 1;
	d = cc_warn_begin(c, "tautological-pointer-compare", true, NULL, at,
			  "comparison of %s'%s' %s to a null pointer is always %s", what, nm, eq ? "equal" : "not equal",
			  eq ? "false" : "true");
	if (d) {
		pp_diag_label(c->p, d, xr.b, xr.e, NULL);
		pp_diag_label(c->p, d, nr.b, nr.e, NULL);
		if (fn) {
			const ctype *ft = T(c, y->ty);
			vdiag *k = pp_diag_note(c->p, d, xr.b, "prefix with the address-of operator to silence this warning");

			if (k)
				pp_diag_fix(c->p, k, xr.b, xr.b, "&", VDA_MAYBE);
			if (!ft->n && !(ft->flags & TF_VARIADIC) && !is_void(c, ft->base)) {
				k = pp_diag_note(c->p, d, xr.b, "suffix with parentheses to turn this into a function call");
				if (k)
					pp_diag_fix(c->p, k, xr.e, xr.e, "()", VDA_MAYBE);
			}
		} else {
			pp_diag_help(c->p, d, "%s'%s' is never null", what, nm);
		}
		pp_diag_emit(c->p, d);
	}
}

static inline void ptr_cmp(cc *c, int op, uint32_t l, uint32_t r)
{
	if (op != T_EQUALEQUAL && op != T_EXCLAIMEQ)
		return;
	if (is_npc(c, r) && !is_npc(c, l))
		null_compare(c, op, l, r);
	else if (is_npc(c, l) && !is_npc(c, r))
		null_compare(c, op, r, l);
}

/* clang's -Wtautological-constant-out-of-range-compare: a constant the other operand's own type
   (implicit conversions stripped) cannot hold, compared in the comparison's type */
static __attribute__((cold, noinline)) void range_compare(cc *c, uint32_t n, int nk, uint32_t loc)
{
	const node *x = N(n);
	int op = x->aux;
	uint32_t k = nk ? x->b : x->a, o = nk ? x->a : x->b, os = no_implicit(c, o);
	qty ot = N(os)->ty;
	cval v, ov;
	i128 lo, hi;
	uint64_t w;
	bool above, res;
	char vs[48];
	pp_span orr = rng(c, os), kr = rng(c, no_implicit(c, k));
	vdiag *d;

	if (!is_integer(c, ot) || (cc_eval(c, os, &ov) && ov.kind == CV_INT) || !cc_eval(c, k, &v) || v.kind != CV_INT)
		return;
	w = ty_size(c, ot) * 8;
	if (is_signed(c, ot) && !is_signed(c, N(k)->ty) && KIND(c, ot) != TY_BOOL)
		return;	/* a signed operand in an unsigned comparison: not one interval (clang) */
	if (KIND(c, ot) == TY_BOOL)
		lo = 0, hi = 1;
	else if (w >= 64 || w >= ty_size(c, N(k)->ty) * 8)
		return;	/* as wide as the comparison: every converted constant fits */
	else if (is_signed(c, ot))
		lo = -((i128)1 << (w - 1)), hi = ((i128)1 << (w - 1)) - 1;
	else
		lo = 0, hi = ((i128)1 << w) - 1;
	if (v.i >= lo && v.i <= hi)
		return;
	above = v.i > hi;
	if (!nk)	/* constant on the left: K op X is X op' K */
		op = op == T_LESS ? T_GREATER : op == T_GREATER ? T_LESS : op == T_LESSEQ ? T_GREATEREQ : op == T_GREATEREQ ? T_LESSEQ : op;
	switch (op) {
	case T_EQUALEQUAL: res = false; break;
	case T_EXCLAIMEQ: res = true; break;
	case T_LESS: case T_LESSEQ: res = above; break;
	default: res = !above; break;
	}
	i128s_(vs, sizeof vs, v.i);
	d = cc_warn_begin(c, "tautological-constant-out-of-range-compare", true, NULL, loc,
			  "result of comparison of constant %s with expression of type '%s' is always %s", vs, tyname(c, ot),
			  res ? "true" : "false");
	if (!d)
		return;
	if (nk)
		pp_diag_label(c->p, d, orr.b, orr.e, NULL), pp_diag_label(c->p, d, kr.b, kr.e, NULL);
	else
		pp_diag_label(c->p, d, kr.b, kr.e, NULL), pp_diag_label(c->p, d, orr.b, orr.e, NULL);
	{
		char ls[48], hs[48];

		i128s_(ls, sizeof ls, lo), i128s_(hs, sizeof hs, hi);
		pp_diag_help(c->p, d, "'%s' holds %s to %s%s", tyname(c, ot), ls, hs, char_sign(c, ot));
	}
	rt_emit(c, d);
}

static inline void range_cmp(cc *c, uint32_t n, uint32_t loc)
{
	const node *x = N(n);
	cval v;

	if (x->op != N_BINARY || !is_integer(c, N(x->a)->ty) || !is_integer(c, N(x->b)->ty))
		return;
	{	/* cheap first: an operand narrower than the comparison (or bool) */
		uint64_t cw = ty_size(c, N(x->a)->ty);
		qty ta = N(no_implicit(c, x->a))->ty, tb = N(no_implicit(c, x->b))->ty;
		bool na = is_integer(c, ta) && (ty_size(c, ta) < cw || KIND(c, ta) == TY_BOOL);
		bool nb = is_integer(c, tb) && (ty_size(c, tb) < cw || KIND(c, tb) == TY_BOOL);

		if (likely(!na && !nb))
			return;
		if (na && cc_eval(c, x->b, &v) && v.kind == CV_INT)
			range_compare(c, n, 1, loc);
		else if (nb && cc_eval(c, x->a, &v) && v.kind == CV_INT)
			range_compare(c, n, 0, loc);
	}
}

static inline void logic_const(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	cval v;

	if (!is_integer(c, N(r)->ty) || !is_integer(c, N(l)->ty) || KIND(c, N(l)->ty) == TY_BOOL ||
	    !cc_eval(c, r, &v) || v.kind != CV_INT || v.i == 0 || v.i == 1)
		return;
	if (N(r)->op == N_NUM && (N(r)->aux & NUM_ENUM))
		return;
	if (N(l)->op == N_NUM && (N(l)->aux & NUM_ENUM)) {
		cval lv;

		if (cc_eval(c, l, &lv) && lv.kind == CV_INT && lv.i != 0 && lv.i != 1)
			return;
	}
	logic_const_warn(c, op, l, r, loc, v.i);
}


/* -Winteger-overflow: n, a signed integer +, -, * or unary - of constants,
   whose exact value does not fit its type.  Once per expression (NF_OVF). */
static void ovf_warn(cc *c, int op, uint32_t n, uint32_t loc, uint32_t sub0, uint32_t sub1)
{
	cval a, b;
	i128 v;
	int bits;
	qty t = N(n)->ty;

	if ((N(sub0)->flags & NF_OVF) || (sub1 && (N(sub1)->flags & NF_OVF))) {
		N(n)->flags |= NF_OVF;
		return;
	}
	if (c->dead || c->unevaluated || !is_integer(c, t) || !is_signed(c, t) || (bits = (int)ty_size(c, t) * 8) > 64 ||
	    !cc_eval(c, sub0, &a) || a.kind != CV_INT || (sub1 && (!cc_eval(c, sub1, &b) || b.kind != CV_INT)))
		return;
	if (!sub1)
		v = -a.i;
	else if (op == T_PLUS)
		v = a.i + b.i;
	else if (op == T_MINUS)
		v = a.i - b.i;
	else
		v = a.i * b.i;
	if (v >= -((i128)1 << (bits - 1)) && v < ((i128)1 << (bits - 1)))
		return;
	N(n)->flags |= NF_OVF;
	{
		char d[48], o[64];
		int64_t w = bits == 64 ? (int64_t)(uint64_t)v : (int64_t)(int32_t)(uint32_t)v;
		uint64_t m = w < 0 ? -(uint64_t)w : (uint64_t)w;
		int nd = 0, k = 0;
		pp_span r = rng(c, n);

		do
			d[nd++] = (char)('0' + m % 10), m /= 10;
		while (m);
		if (w < 0)
			o[k++] = '-';
		while (nd) {
			o[k++] = d[--nd];
			if (nd && nd % 3 == 0)
				o[k++] = '\'';
		}
		o[k] = 0;
		cc_warnx(c, "integer-overflow", true, NULL, loc, &r, 1, NULL, 0,
			 "overflow in expression; result is %s with type '%s'", o, tyname(c, t));
	}
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

			bool lg = op == T_AMPAMP || op == T_PIPEPIPE;

			c->dead += dead;
			c->cnd += lg;	/* (a conditional branch: -Wsometimes-uninitialized's filter) */
			r = binary(c, p + 1);
			c->cnd -= lg;
			c->dead -= dead;
		}
		if (unlikely(ISERR(l) || ISERR(r))) {
			l = err_node(c, loc);
			if (mac)
				N(l)->flags |= NF_MACRO;
			setspan(c, l, b);
		} else {
			uint32_t l0 = l;

			if ((op == T_AMPAMP || op == T_PIPEPIPE) && !mac)
				logic_const(c, op, l, r, loc);
			else if (is_cmp(op) && !mac) {
				self_cmp(c, op, l, r, loc);
				if (unlikely(is_ptr(c, N(l)->ty) || is_ptr(c, N(r)->ty) || is_array(c, N(l)->ty) || is_array(c, N(r)->ty) ||
					     is_func(c, N(l)->ty) || is_func(c, N(r)->ty)))
					ptr_cmp(c, op, l, r);
			}
			runtime_warn(c, op, l, r, loc);
			l = binop(c, op, l, r, loc);
			if (mac)
				N(l)->flags |= NF_MACRO;
			setspan(c, l, b);
			if (is_cmp(op) && !mac && !quiet_rt(c))
				range_cmp(c, l, loc);
			if (op == T_PLUS || op == T_MINUS || op == T_STAR)
				ovf_warn(c, op, l, loc, l0, r);
			else if ((N(l0)->flags | N(r)->flags) & NF_OVF)
				N(l)->flags |= NF_OVF;
		}
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
		if (!is_bool_vector(c, N(cnd)->ty))
			cnd = scalar_cond(c, decay(c, cnd), "conditional");
		k = cc_eval(c, cnd, &cv) && cv.kind == CV_INT ? cv.i != 0 : -1;
		c->dead += k == 0;
		c->cnd++;
		if (!is(c, T_COLON))
			a = expr(c);
		else
			EXT_WARN_MS(c, ":", "gnu-conditional-omitted-operand", w_gnu, tok(c)->loc,
				 "use of GNU ?: conditional expression extension, omitting middle operand");
		c->dead -= k == 0;
		expect(c, T_COLON, "':'");
		c->dead += k == 1;
		b = cond_expr(c);
		c->cnd--;
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
	{
		uint32_t b = rng(c, cnd).b, n = cond_rest(c, cnd);

		setspan(c, n, b);	/* (clang: a ?: from its condition) */
		return n;
	}
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

/* -Wself-assign: x = x (parentheses and implicit conversions aside), not volatile */
static void self_assign(cc *c, uint32_t l, uint32_t r, uint32_t loc)
{
	while (N(r)->op == N_CAST && !c->span[r].e && N(r)->a)
		r = N(r)->a;
	if (N(l)->op != N_VAR || N(r)->op != N_VAR || N(l)->a != N(r)->a || (QQ(N(l)->ty) & Q_VOLATILE) ||
	    c->syms[N(l)->a].kind != S_VAR)
		return;
	{
		uint32_t nm = c->syms[N(l)->a].name;

		if (!pp_spells(c->p, N(l)->loc, idstr_(nm), (uint32_t)idlen_(nm)) ||
		    !pp_spells(c->p, N(r)->loc, idstr_(nm), (uint32_t)idlen_(nm)))
			return;	/* (clang: not from a macro) */
	}
	{
		pp_span rr[2] = { { N(l)->loc, pp_tok_end(c->p, N(l)->loc) }, { N(r)->loc, pp_tok_end(c->p, N(r)->loc) } };

		cc_warnx(c, "self-assign", false, w_selfassign, loc, rr, 2, NULL, 0,
			 "explicitly assigning value of variable of type '%s' to itself", tyname(c, N(l)->ty));
	}
}

static __attribute__((noinline)) uint32_t assign_rest(cc *c, uint32_t l, int op)
{
	uint32_t loc = tok(c)->loc, r;
	bool mac = tok(c)->flags & PF_MACRO;

	next(c);
	{
		uint32_t pend = c->uv_asg;

		c->uv_asg = 0;
		r = assign_expr(c);
		if (unlikely(pend)) {
			c->syms[pend].flags &= ~SF_UVPEND;	/* (after the right side: "z = z + 1" reads z first) */
			c->uv_asg = 0;
		}
	}
	if (ISERR(l) || ISERR(r))
		return err_node(c, loc);
	if (op < 0 && !mac && !(c->prevflags & PF_MACRO))	/* (the right side's last token) */
		self_assign(c, l, r, loc);
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
		if (unlikely(ISERR(l) || ISERR(r))) {
			l = err_node(c, loc);
			continue;
		}
		unused_value(c, l, true);
		set_only(c, l);	/* (a discarded value: clang's IgnoredValueConversions) */
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
	uint32_t n;

	c->cexpr++;
	n = rvalue(c, cond_expr(c));
	c->cexpr--;
	if (np)
		*np = n;
	if (!const_int(c, n, v)) {
		cc_error(c, N(n)->loc, "expression is not an integer constant expression");
		*v = 0;
		return false;
	}
	return true;
}
