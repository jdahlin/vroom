/* vcc front end: shuffle, convertvector, lane builtins and elementwise abs; see builtin-table.c for where a builtin's other facts live */
#include "cc-internal.h"

/*
 * __builtin_shufflevector(a, b, i...): lanes of a then b picked by constant
 * indices (-1: undefined, here 0), a vector of as many lanes;
 * __builtin_convertvector(v, AST_TYPE): each lane converted to AST_TYPE's lane type.
 * Both lowered lane by lane in arch/<cpu>/codegen.
 */
CC_PRIVATE uint32_t cc_builtin_vector_build(cc *c, bool shuffle, uint32_t loc)
{
	cc_id_list args;
	uint32_t a, b;
	qty at, rt;

	cc_id_list_init(&args);
	cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
	a = cc_node_rvalue(c, cc_assignment_parse(c));
	at = QUNQ(AST_NODE(a)->ty);
	cc_token_expect(c, LEX_TOKEN_COMMA, "','");
	if (!shuffle) {
		rt = cc_type_name_parse(c);
		cc_token_expect_here(c, LEX_TOKEN_RIGHT_PAREN, "')'");
		if (!cc_type_is_vector(c, at) || !cc_type_is_vector(c, rt) ||
		    (AST_TYPE(c, at)->ext ?: AST_TYPE(c, at)->n) != (AST_TYPE(c, rt)->ext ?: AST_TYPE(c, rt)->n)) {
			cc_report_error(c, loc, "first two arguments to __builtin_convertvector must have the same number of elements");
			return cc_node_integer_new(c, 0, cc_type_basic(TY_INT), loc);
		}
		cc_id_list_append(c, &args, a);
		return cc_builtin_node_new(c, BI_CONVERTVECTOR, rt, loc, &args);
	}
	b = cc_node_rvalue(c, cc_assignment_parse(c));
	cc_id_list_append(c, &args, a);
	cc_id_list_append(c, &args, b);
	if (!cc_type_is_vector(c, at) || !cc_type_is_compatible(c, at, QUNQ(AST_NODE(b)->ty))) {
		cc_report_error(c, loc, "first two arguments to '__builtin_shufflevector' must have the same type");
		while (cc_token_accept(c, LEX_TOKEN_COMMA))
			cc_assignment_parse(c);
		cc_token_expect_here(c, LEX_TOKEN_RIGHT_PAREN, "')'");
		return cc_node_integer_new(c, 0, cc_type_basic(TY_INT), loc);
	}
	{
		qty et = AST_TYPE(c, at)->base;
		uint64_t n = AST_TYPE(c, at)->ext ?: AST_TYPE(c, at)->n, k = 0;

		while (cc_token_accept(c, LEX_TOKEN_COMMA)) {
			uint32_t ix = cc_assignment_parse(c);
			i128 v = 0;

			if (!cc_constant_integer(c, ix, &v) || v < -1 || v >= (i128)(2 * n))
				cc_report_error(c, AST_NODE(ix)->loc, "index for __builtin_shufflevector must be a constant integer in range");
			cc_id_list_append(c, &args, cc_node_integer_new(c, v, cc_type_basic(TY_INT), AST_NODE(ix)->loc));
			k++;
		}
		cc_token_expect_here(c, LEX_TOKEN_RIGHT_PAREN, "')'");
		if (k == 0 || (cc_type_is_bool_vector(c, at) ? k > 4096 : ((k & (k - 1)) || k * cc_type_size(c, et) > 64))) {
			cc_report_error(c, loc, "__builtin_shufflevector: the number of indices must be a power of two");
			return cc_node_integer_new(c, 0, cc_type_basic(TY_INT), loc);
		}
		if (cc_type_is_bool_vector(c, at)) {
			uint32_t size = 1;
			while (size < (k + 7) / 8) size *= 2;
			ctype t = *AST_TYPE(c, cc_type_vector(c, et, size));
			t.ext = (uint16_t)k, t.n = (uint32_t)k;
			rt = QT(cc_type_intern(c, &t), 0);
		} else
			rt = cc_type_vector(c, et, (uint32_t)(k * cc_type_size(c, et)));
	}
	return cc_builtin_node_new(c, BI_SHUFFLEVECTOR, rt, loc, &args);
}

/* SSE operations expressed in ordinary vector lanes, preserving argument
 * evaluation and the instruction's second-operand choice for NaNs and ties. */
CC_PRIVATE uint32_t cc_builtin_vector_lane(cc *c, uint32_t v, qty et, unsigned i, uint32_t loc)
{
	uint32_t p = cc_cast_insert(c, cc_node_rvalue(c, v), cc_type_pointer(c, et), CK_VADDR);
	uint32_t n = cc_node_new_operands(c, N_INDEX, et, loc, p, cc_node_integer_new(c, i, cc_type_basic(TY_LONG), loc));
	AST_NODE(n)->flags = NF_LVALUE;
	return n;
}

/* abs has no integer promotions and INT_MIN maps to itself. Do the
 * negation in the unsigned type so that the generated operation cannot
 * acquire signed-overflow semantics. */
static uint32_t cc_builtin_elementwise_abs_lane(cc *c, uint32_t a, qty et, uint32_t loc)
{
	if (cc_type_is_integer(c, et)) {
		qty u = cc_type_unsigned(c, et);
		uint32_t neg = cc_conversion_apply(c, cc_node_binary_new(c, LEX_TOKEN_MINUS, u, cc_node_integer_new(c, 0, u, loc), cc_conversion_apply(c, a, u), loc), et);
		uint32_t cond = cc_node_binary_new(c, LEX_TOKEN_LESS, cc_type_basic(TY_INT), a, cc_node_integer_new(c, 0, et, loc), loc);
		return cc_conversion_apply(c, cc_conditional_build(c, cond, neg, a, loc), et);
	}
	if (cc_type_size(c, et) == 2) {
		/* Clearing the sign bit directly also preserves signaling NaNs;
		 * converting through float would quiet them. */
		uint32_t sym = cc_symbol_new(c, S_VAR, 0, et, loc);
		c->syms[sym].sc = SC_AUTO;
		uint32_t tmp = cc_node_new(c, N_VAR, et, loc); AST_NODE(tmp)->a = sym; AST_NODE(tmp)->flags = NF_LVALUE;
		uint32_t p = cc_cast_insert(c, cc_address_of_build(c, tmp, loc), cc_type_pointer(c, cc_type_basic(TY_USHORT)), CK_P2P);
		uint32_t bits = cc_node_new(c, N_DEREF, cc_type_basic(TY_USHORT), loc); AST_NODE(bits)->a = p; AST_NODE(bits)->flags = NF_LVALUE;
		uint32_t masked = cc_node_binary_new(c, LEX_TOKEN_AMPERSAND, cc_type_basic(TY_USHORT), cc_node_rvalue(c, bits), cc_node_integer_new(c, 0x7fff, cc_type_basic(TY_USHORT), loc), loc);
		uint32_t write = cc_node_new_operands(c, N_ASSIGN, cc_type_basic(TY_USHORT), loc, bits, masked);
		return cc_lowering_sequence(c, cc_node_new_operands(c, N_ASSIGN, et, loc, tmp, a), cc_lowering_sequence(c, write, cc_node_rvalue(c, tmp)));
	}
	const char *name = AST_TYPE_KIND(c, et) == TY_FLOAT ? "__builtin_fabsf" : AST_TYPE_KIND(c, et) == TY_DOUBLE ? "__builtin_fabs" : "__builtin_fabsl";
	const cc_builtin *b = cc_builtin_table_get();
	while (strcmp(b->name, name)) b++;
	qty ft = cc_builtin_type(c, b);
	uint32_t sym = cc_symbol_new(c, S_BUILTIN, 0, ft, loc);
	c->syms[sym].val = b - cc_builtin_table_get();
	uint32_t fn = cc_node_new(c, N_VAR, ft, loc); AST_NODE(fn)->a = sym;
	cc_id_list args; cc_id_list_init(&args); cc_id_list_append(c, &args, a);
	uint32_t n = cc_node_new(c, N_CALL, et, loc);
	AST_NODE(n)->a = cc_conversion_decay(c, fn); AST_NODE(n)->b = cc_id_list_freeze(c, &args); AST_NODE(n)->c = 1;
	return n;
}

/* A folded argument may still contain evaluated comma operands. This
 * check includes volatile/atomic reads and respects short-circuiting, while
 * leaving genuinely constant arguments free of synthetic comma expressions
 * (which would make a C23 constexpr initializer nonconstant). */
CC_PRIVATE bool cc_builtin_has_effects(cc *c, uint32_t n, bool load)
{
	if (!n) return false;
	const node *x = AST_NODE(n);
	if (load && (x->flags & NF_LVALUE) && ((QQ(x->ty) & Q_VOLATILE) || (AST_TYPE(c, x->ty)->flags & TF_ATOMIC)))
		return true;
	switch (x->op) {
	case N_NUM: case N_FNUM: case N_ZERO: case N_STR: case N_VAR: case N_LABELADDR:
		return false;
	case N_ADDR: case N_MEMBER:
		return cc_builtin_has_effects(c, x->a, false);
	case N_CAST:
		return cc_builtin_has_effects(c, x->a, x->aux != CK_DECAY && x->aux != CK_FDECAY);
	case N_UNARY: case N_DEREF:
		return cc_builtin_has_effects(c, x->a, true);
	case N_INDEX: case N_BINARY: case N_COMMA: case N_CPLX:
		return cc_builtin_has_effects(c, x->a, true) || cc_builtin_has_effects(c, x->b, true);
	case N_LOGAND: case N_LOGOR: case N_COND: {
		cval cv;
		if (cc_builtin_has_effects(c, x->a, true)) return true;
		if (cc_eval(c, x->a, &cv)) {
			bool truth = cv.kind == CV_INT ? cv.i != 0 : cv.kind == CV_FLT ? (cv.ld ? vsoftfloat_is_nonzero(cv.q) : cv.f != 0) : true;
			if (x->op == N_COND)
				return cc_builtin_has_effects(c, truth ? x->b : x->c, true);
			return (x->op == N_LOGAND ? truth : !truth) && cc_builtin_has_effects(c, x->b, true);
		}
		return cc_builtin_has_effects(c, x->b, true) || cc_builtin_has_effects(c, x->c, true);
	}
	case N_BUILTIN: case N_CALL: {
		if (x->op == N_BUILTIN && x->aux == BI_CONSTANT_P) return false;
		cval value;
		if (!cc_eval(c, n, &value)) return true;
		if (x->op == N_CALL) {
			uint32_t fn = x->a;
			while (AST_NODE(fn)->op == N_CAST) fn = AST_NODE(fn)->a;
			if (AST_NODE(fn)->op != N_VAR || c->syms[AST_NODE(fn)->a].kind != S_BUILTIN) return true;
		}
		for (uint32_t i = 0; i < x->c; i++)
			if (cc_builtin_has_effects(c, c->lists[x->b + i], true)) return true;
		return false;
	}
	default:
		return true;
	}
}

CC_PRIVATE uint32_t cc_builtin_elementwise_abs_build(cc *c, const cc_builtin *b, uint32_t loc)
{
	cc_id_list args;
	cc_builtin_arguments_parse(c, &args);
	if (args.n != 1) { cc_builtin_arity_check(c, loc, 1, &args); return cc_node_error_new(c, loc); }
	uint32_t arg = cc_conversion_decay(c, args.v[0]);
	qty t = QUNQ(AST_NODE(arg)->ty), et = cc_type_is_vector(c, t) ? AST_TYPE(c, t)->base : t;
	int k = AST_TYPE_KIND(c, et);
	if ((!cc_type_is_float(c, et) && (!cc_type_is_integer(c, et) || !cc_type_is_signed(c, et))) || k == TY_ENUM || k == TY_BOOL) {
		cc_report_error(c, loc, "argument to '%s' must be a scalar or vector of signed integer or floating-point type", b->name);
		return cc_node_error_new(c, loc);
	}
	/* Retain constant-expression support for scalar integer/floating args. */
	cval cv;
	if (!cc_type_is_vector(c, t) && cc_eval(c, arg, &cv) && (cv.kind == CV_FLT || cv.kind == CV_INT)) {
		/* A known value need not be a pure expression: constant evaluation
		 * of a comma expression may ignore an observable left operand.
		 * Evaluate the original exactly once, then return only its folded
		 * magnitude. Reusing arg in the integer select would evaluate it twice. */
		uint32_t folded;
		if (cv.kind == CV_FLT)
			folded = cv.ld ? cc_node_float128_new(c, cv.q & ~((u128)1 << 127), et, loc) : cc_node_float_new(c, __builtin_fabs(cv.f), et, loc);
		else
			folded = cc_node_integer_new(c, cc_constant_truncate(c, cv.i < 0 ? (i128)(0 - (u128)cv.i) : cv.i, et), et, loc);
		return cc_builtin_has_effects(c, arg, true) ? cc_lowering_sequence(c, arg, folded) : folded;
	}
	uint32_t sym = cc_symbol_new(c, S_VAR, 0, t, loc);
	c->syms[sym].sc = SC_AUTO;
	uint32_t saved = cc_node_new(c, N_VAR, t, loc); AST_NODE(saved)->a = sym; AST_NODE(saved)->flags = NF_LVALUE;
	uint32_t pre = cc_node_new_operands(c, N_ASSIGN, t, loc, saved, arg);
	if (!cc_type_is_vector(c, t))
		return cc_lowering_sequence(c, pre, cc_builtin_elementwise_abs_lane(c, cc_node_rvalue(c, saved), et, loc));
	sym = cc_symbol_new(c, S_VAR, 0, t, loc); c->syms[sym].sc = SC_AUTO;
	uint32_t out = cc_node_new(c, N_VAR, t, loc); AST_NODE(out)->a = sym; AST_NODE(out)->flags = NF_LVALUE;
	uint32_t result = cc_node_rvalue(c, out);
	for (unsigned i = cc_type_size(c, t) / cc_type_size(c, et); i--;) {
		uint32_t lane = cc_node_rvalue(c, cc_builtin_vector_lane(c, saved, et, i, loc));
		uint32_t value = cc_builtin_elementwise_abs_lane(c, lane, et, loc);
		uint32_t dst = cc_builtin_vector_lane(c, out, et, i, loc);
		result = cc_lowering_sequence(c, cc_node_new_operands(c, N_ASSIGN, et, loc, dst, value), result);
	}
	return cc_lowering_sequence(c, pre, result);
}
