/* vcc front end: shuffle, convertvector, lane builtins and elementwise abs; see builtin-table.c for where a builtin's other facts live */
#include "ccimpl.h"

/*
 * __builtin_shufflevector(a, b, i...): lanes of a then b picked by constant
 * indices (-1: undefined, here 0), a vector of as many lanes;
 * __builtin_convertvector(v, T): each lane converted to T's lane type.
 * Both lowered lane by lane in arch/<cpu>/codegen.
 */
VPRIV uint32_t vector_builtin(cc *c, bool shuffle, uint32_t loc)
{
	vec args;
	uint32_t a, b;
	qty at, rt;

	vinit(&args);
	expect(c, T_LPAREN, "'('");
	a = rvalue(c, assign_expr(c));
	at = QUNQ(N(a)->ty);
	expect(c, T_COMMA, "','");
	if (!shuffle) {
		rt = type_name(c);
		expect_here(c, T_RPAREN, "')'");
		if (!is_vector(c, at) || !is_vector(c, rt) ||
		    (T(c, at)->ext ?: T(c, at)->n) != (T(c, rt)->ext ?: T(c, rt)->n)) {
			cc_error(c, loc, "first two arguments to __builtin_convertvector must have the same number of elements");
			return mk_int(c, 0, prim(TY_INT), loc);
		}
		vpush(c, &args, a);
		return mk_builtin(c, BI_CONVERTVECTOR, rt, loc, &args);
	}
	b = rvalue(c, assign_expr(c));
	vpush(c, &args, a);
	vpush(c, &args, b);
	if (!is_vector(c, at) || !compatible(c, at, QUNQ(N(b)->ty))) {
		cc_error(c, loc, "first two arguments to '__builtin_shufflevector' must have the same type");
		while (accept(c, T_COMMA))
			assign_expr(c);
		expect_here(c, T_RPAREN, "')'");
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	{
		qty et = T(c, at)->base;
		uint64_t n = T(c, at)->ext ?: T(c, at)->n, k = 0;

		while (accept(c, T_COMMA)) {
			uint32_t ix = assign_expr(c);
			i128 v = 0;

			if (!const_int(c, ix, &v) || v < -1 || v >= (i128)(2 * n))
				cc_error(c, N(ix)->loc, "index for __builtin_shufflevector must be a constant integer in range");
			vpush(c, &args, mk_int(c, v, prim(TY_INT), N(ix)->loc));
			k++;
		}
		expect_here(c, T_RPAREN, "')'");
		if (k == 0 || (is_bool_vector(c, at) ? k > 4096 : ((k & (k - 1)) || k * ty_size(c, et) > 64))) {
			cc_error(c, loc, "__builtin_shufflevector: the number of indices must be a power of two");
			return mk_int(c, 0, prim(TY_INT), loc);
		}
		if (is_bool_vector(c, at)) {
			uint32_t size = 1;
			while (size < (k + 7) / 8) size *= 2;
			ctype t = *T(c, ty_vector(c, et, size));
			t.ext = (uint16_t)k, t.n = (uint32_t)k;
			rt = QT(ty_intern(c, &t), 0);
		} else
			rt = ty_vector(c, et, (uint32_t)(k * ty_size(c, et)));
	}
	return mk_builtin(c, BI_SHUFFLEVECTOR, rt, loc, &args);
}

/* SSE operations expressed in ordinary vector lanes, preserving argument
 * evaluation and the instruction's second-operand choice for NaNs and ties. */
VPRIV uint32_t builtin_vector_lane(cc *c, uint32_t v, qty et, unsigned i, uint32_t loc)
{
	uint32_t p = cast_node(c, rvalue(c, v), ty_ptr(c, et), CK_VADDR);
	uint32_t n = mk2(c, N_INDEX, et, loc, p, mk_int(c, i, prim(TY_LONG), loc));
	N(n)->flags = NF_LVALUE;
	return n;
}

/* abs has no integer promotions and INT_MIN maps to itself. Do the
 * negation in the unsigned type so that the generated operation cannot
 * acquire signed-overflow semantics. */
static uint32_t elementwise_abs_lane(cc *c, uint32_t a, qty et, uint32_t loc)
{
	if (is_integer(c, et)) {
		qty u = unsigned_of(c, et);
		uint32_t neg = convert(c, mk_bin(c, T_MINUS, u, mk_int(c, 0, u, loc), convert(c, a, u), loc), et);
		uint32_t cond = mk_bin(c, T_LESS, prim(TY_INT), a, mk_int(c, 0, et, loc), loc);
		return convert(c, conditional(c, cond, neg, a, loc), et);
	}
	if (ty_size(c, et) == 2) {
		/* Clearing the sign bit directly also preserves signaling NaNs;
		 * converting through float would quiet them. */
		uint32_t sym = sym_new(c, S_VAR, 0, et, loc);
		c->syms[sym].sc = SC_AUTO;
		uint32_t tmp = mk(c, N_VAR, et, loc); N(tmp)->a = sym; N(tmp)->flags = NF_LVALUE;
		uint32_t p = cast_node(c, addr_of(c, tmp, loc), ty_ptr(c, prim(TY_USHORT)), CK_P2P);
		uint32_t bits = mk(c, N_DEREF, prim(TY_USHORT), loc); N(bits)->a = p; N(bits)->flags = NF_LVALUE;
		uint32_t masked = mk_bin(c, T_AMP, prim(TY_USHORT), rvalue(c, bits), mk_int(c, 0x7fff, prim(TY_USHORT), loc), loc);
		uint32_t write = mk2(c, N_ASSIGN, prim(TY_USHORT), loc, bits, masked);
		return with_pre(c, mk2(c, N_ASSIGN, et, loc, tmp, a), with_pre(c, write, rvalue(c, tmp)));
	}
	const char *name = KIND(c, et) == TY_FLOAT ? "__builtin_fabsf" : KIND(c, et) == TY_DOUBLE ? "__builtin_fabs" : "__builtin_fabsl";
	const builtin *b = builtin_all();
	while (strcmp(b->name, name)) b++;
	qty ft = builtin_type(c, b);
	uint32_t sym = sym_new(c, S_BUILTIN, 0, ft, loc);
	c->syms[sym].val = b - builtin_all();
	uint32_t fn = mk(c, N_VAR, ft, loc); N(fn)->a = sym;
	vec args; vinit(&args); vpush(c, &args, a);
	uint32_t n = mk(c, N_CALL, et, loc);
	N(n)->a = decay(c, fn); N(n)->b = vfreeze(c, &args); N(n)->c = 1;
	return n;
}

/* A folded argument may still contain evaluated comma operands. This
 * check includes volatile/atomic reads and respects short-circuiting, while
 * leaving genuinely constant arguments free of synthetic comma expressions
 * (which would make a C23 constexpr initializer nonconstant). */
VPRIV bool elementwise_abs_effects(cc *c, uint32_t n, bool load)
{
	if (!n) return false;
	const node *x = N(n);
	if (load && (x->flags & NF_LVALUE) && ((QQ(x->ty) & Q_VOLATILE) || (T(c, x->ty)->flags & TF_ATOMIC)))
		return true;
	switch (x->op) {
	case N_NUM: case N_FNUM: case N_ZERO: case N_STR: case N_VAR: case N_LABELADDR:
		return false;
	case N_ADDR: case N_MEMBER:
		return elementwise_abs_effects(c, x->a, false);
	case N_CAST:
		return elementwise_abs_effects(c, x->a, x->aux != CK_DECAY && x->aux != CK_FDECAY);
	case N_UNARY: case N_DEREF:
		return elementwise_abs_effects(c, x->a, true);
	case N_INDEX: case N_BINARY: case N_COMMA: case N_CPLX:
		return elementwise_abs_effects(c, x->a, true) || elementwise_abs_effects(c, x->b, true);
	case N_LOGAND: case N_LOGOR: case N_COND: {
		cval cv;
		if (elementwise_abs_effects(c, x->a, true)) return true;
		if (cc_eval(c, x->a, &cv)) {
			bool truth = cv.kind == CV_INT ? cv.i != 0 : cv.kind == CV_FLT ? (cv.ld ? q_nz(cv.q) : cv.f != 0) : true;
			if (x->op == N_COND)
				return elementwise_abs_effects(c, truth ? x->b : x->c, true);
			return (x->op == N_LOGAND ? truth : !truth) && elementwise_abs_effects(c, x->b, true);
		}
		return elementwise_abs_effects(c, x->b, true) || elementwise_abs_effects(c, x->c, true);
	}
	case N_BUILTIN: case N_CALL: {
		if (x->op == N_BUILTIN && x->aux == BI_CONSTANT_P) return false;
		cval value;
		if (!cc_eval(c, n, &value)) return true;
		if (x->op == N_CALL) {
			uint32_t fn = x->a;
			while (N(fn)->op == N_CAST) fn = N(fn)->a;
			if (N(fn)->op != N_VAR || c->syms[N(fn)->a].kind != S_BUILTIN) return true;
		}
		for (uint32_t i = 0; i < x->c; i++)
			if (elementwise_abs_effects(c, c->lists[x->b + i], true)) return true;
		return false;
	}
	default:
		return true;
	}
}

VPRIV uint32_t elementwise_abs_call(cc *c, const builtin *b, uint32_t loc)
{
	vec args;
	args_list(c, &args);
	if (args.n != 1) { arity(c, loc, 1, &args); return err_node(c, loc); }
	uint32_t arg = decay(c, args.v[0]);
	qty t = QUNQ(N(arg)->ty), et = is_vector(c, t) ? T(c, t)->base : t;
	int k = KIND(c, et);
	if ((!is_float(c, et) && (!is_integer(c, et) || !is_signed(c, et))) || k == TY_ENUM || k == TY_BOOL) {
		cc_error(c, loc, "argument to '%s' must be a scalar or vector of signed integer or floating-point type", b->name);
		return err_node(c, loc);
	}
	/* Retain constant-expression support for scalar integer/floating args. */
	cval cv;
	if (!is_vector(c, t) && cc_eval(c, arg, &cv) && (cv.kind == CV_FLT || cv.kind == CV_INT)) {
		/* A known value need not be a pure expression: constant evaluation
		 * of a comma expression may ignore an observable left operand.
		 * Evaluate the original exactly once, then return only its folded
		 * magnitude. Reusing arg in the integer select would evaluate it twice. */
		uint32_t folded;
		if (cv.kind == CV_FLT)
			folded = cv.ld ? mk_fltq(c, cv.q & ~((u128)1 << 127), et, loc) : mk_flt(c, __builtin_fabs(cv.f), et, loc);
		else
			folded = mk_int(c, trunc_to(c, cv.i < 0 ? (i128)(0 - (u128)cv.i) : cv.i, et), et, loc);
		return elementwise_abs_effects(c, arg, true) ? with_pre(c, arg, folded) : folded;
	}
	uint32_t sym = sym_new(c, S_VAR, 0, t, loc);
	c->syms[sym].sc = SC_AUTO;
	uint32_t saved = mk(c, N_VAR, t, loc); N(saved)->a = sym; N(saved)->flags = NF_LVALUE;
	uint32_t pre = mk2(c, N_ASSIGN, t, loc, saved, arg);
	if (!is_vector(c, t))
		return with_pre(c, pre, elementwise_abs_lane(c, rvalue(c, saved), et, loc));
	sym = sym_new(c, S_VAR, 0, t, loc); c->syms[sym].sc = SC_AUTO;
	uint32_t out = mk(c, N_VAR, t, loc); N(out)->a = sym; N(out)->flags = NF_LVALUE;
	uint32_t result = rvalue(c, out);
	for (unsigned i = ty_size(c, t) / ty_size(c, et); i--;) {
		uint32_t lane = rvalue(c, builtin_vector_lane(c, saved, et, i, loc));
		uint32_t value = elementwise_abs_lane(c, lane, et, loc);
		uint32_t dst = builtin_vector_lane(c, out, et, i, loc);
		result = with_pre(c, mk2(c, N_ASSIGN, et, loc, dst, value), result);
	}
	return with_pre(c, pre, result);
}
