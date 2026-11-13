/* vcc front end: popcount, clz, ctz and the stdc_ bit operations lowered to ordinary code; see builtin-table.c for where a builtin's other facts live */
#include "cc-internal.h"

/* A fixed-width bit operation used by the generic forms. */
static uint32_t cc_builtin_bit_count_fixed(cc *c, int id, uint32_t x, bool wide, uint32_t loc)
{
	cc_id_list args;
	int fid = id == BI_CLZG ? (wide ? BI_CLZLL : BI_CLZ) :
	          id == BI_CTZG ? (wide ? BI_CTZLL : BI_CTZ) :
	                         (wide ? BI_POPCOUNTLL : BI_POPCOUNT);
	cc_id_list_init(&args);
	cc_id_list_append(c, &args, x);
	return cc_builtin_node_new(c, fid, cc_type_basic(TY_INT), loc, &args);
}

/* A compact limb loop keeps the AST independent of the integer precision.
 * Explicit declarations preserve operand snapshots across loop iterations. */
CC_PRIVATE uint32_t cc_lowering_local_new(cc *c, cc_id_list *body, qty t, uint32_t init, uint32_t loc)
{
	uint32_t sym = cc_symbol_new(c, S_VAR, 0, t, loc);
	c->syms[sym].sc = SC_AUTO;
	uint32_t var = cc_node_new(c, N_VAR, t, loc);
	AST_NODE(var)->a = sym;
	AST_NODE(var)->flags = NF_LVALUE;
	cc_id_list_append(c, body, cc_node_new_operands(c, N_DECL, cc_type_basic(TY_VOID), loc, sym, init));
	return var;
}

CC_PRIVATE void cc_lowering_statement_append(cc *c, cc_id_list *body, uint32_t n, uint32_t loc)
{
	cc_id_list_append(c, body, cc_statement_new(c, N_EXPR, loc, n, 0, 0));
}

CC_PRIVATE uint32_t cc_builtin_bit_count_bitint(cc *c, int id, uint32_t x, uint32_t fallback, unsigned bits, uint32_t loc)
{
	cval cv;
	unsigned limbs = (bits + 63) / 64;
	if (cc_eval(c, x, &cv) && cv.kind == CV_BIGINT && cv.ice) {
		unsigned count = 0, words = (bits + 31) / 32;
		bool nonzero = false;
		for (unsigned j = 0; j < words; j++) {
			unsigned i = id == BI_CLZG ? words - 1 - j : j;
			uint32_t w = c->lists[cv.big + i];
			if (i == words - 1 && (bits & 31))
				w &= UINT32_MAX >> (32 - (bits & 31));
			nonzero |= w != 0;
			if (id == BI_POPCOUNTG)
				count += (unsigned)__builtin_popcount(w);
			else if (w) {
				count += (unsigned)(id == BI_CLZG ? __builtin_clz(w) : __builtin_ctz(w));
				break;
			} else count += 32;
		}
		if (!nonzero && fallback)
			return fallback;
		if (id == BI_CLZG) count -= words * 32 - bits;
		return cc_lowering_sequence(c, fallback, cc_node_integer_new(c, count, cc_type_basic(TY_INT), loc));
	}
	cc_id_list body, loop, found;
	cc_id_list_init(&body); cc_id_list_init(&loop); cc_id_list_init(&found);
	uint32_t value = cc_lowering_local_new(c, &body, QUNQ(AST_NODE(x)->ty), x, loc);
	uint32_t fb = fallback ? cc_lowering_local_new(c, &body, cc_type_basic(TY_INT), fallback, loc) : 0;
	uint32_t result = cc_lowering_local_new(c, &body, cc_type_basic(TY_INT), cc_node_integer_new(c, 0, cc_type_basic(TY_INT), loc), loc);
	uint32_t index = cc_lowering_local_new(c, &body, cc_type_basic(TY_INT), cc_node_integer_new(c, id == BI_CLZG ? limbs - 1 : 0, cc_type_basic(TY_INT), loc), loc);
	uint32_t address = cc_node_new(c, N_ADDR, cc_type_pointer(c, AST_NODE(value)->ty), loc);
	AST_NODE(address)->a = value;
	address = cc_cast_insert(c, address, cc_type_pointer(c, cc_type_basic(TY_ULLONG)), CK_NOP);
	uint32_t word = cc_node_rvalue(c, cc_dereference_build(c, cc_binary_build(c, LEX_TOKEN_PLUS, address, cc_node_rvalue(c, index), loc), loc));
	if (bits & 63) {
		uint32_t top = cc_binary_build(c, LEX_TOKEN_EQUAL_EQUAL, cc_node_rvalue(c, index), cc_node_integer_new(c, limbs - 1, cc_type_basic(TY_INT), loc), loc);
		uint32_t mask = cc_node_integer_new(c, (i128)(UINT64_MAX >> (64 - (bits & 63))), cc_type_basic(TY_ULLONG), loc);
		word = cc_conditional_build(c, top, cc_binary_build(c, LEX_TOKEN_AMPERSAND, word, mask, loc), word, loc);
	}
	word = cc_lowering_local_new(c, &loop, cc_type_basic(TY_ULLONG), word, loc);
	uint32_t count = cc_builtin_bit_count_fixed(c, id, cc_node_rvalue(c, word), true, loc);
	if (id == BI_POPCOUNTG) {
		cc_lowering_statement_append(c, &loop, cc_assignment_build(c, LEX_TOKEN_PLUS, result, count, loc), loc);
	} else {
		cc_lowering_statement_append(c, &found, cc_assignment_build(c, LEX_TOKEN_PLUS, result, count, loc), loc);
		cc_id_list_append(c, &found, cc_statement_new(c, N_BREAK, loc, 0, 0, 0));
		uint32_t block = cc_statement_new(c, N_BLOCK, loc, 0, cc_id_list_freeze(c, &found), found.n);
		cc_id_list_append(c, &loop, cc_statement_new(c, N_IF, loc, cc_node_rvalue(c, word), block, 0));
		cc_lowering_statement_append(c, &loop, cc_assignment_build(c, LEX_TOKEN_PLUS, result, cc_node_integer_new(c, 64, cc_type_basic(TY_INT), loc), loc), loc);
	}
	cc_lowering_statement_append(c, &loop, cc_assignment_build(c, id == BI_CLZG ? LEX_TOKEN_MINUS : LEX_TOKEN_PLUS, index, cc_node_integer_new(c, 1, cc_type_basic(TY_INT), loc), loc), loc);
	uint32_t cond = cc_binary_build(c, id == BI_CLZG ? LEX_TOKEN_GREATER_EQUAL : LEX_TOKEN_LESS, cc_node_rvalue(c, index),
	                     cc_node_integer_new(c, id == BI_CLZG ? 0 : limbs, cc_type_basic(TY_INT), loc), loc);
	uint32_t block = cc_statement_new(c, N_BLOCK, loc, 0, cc_id_list_freeze(c, &loop), loop.n);
	cc_id_list_append(c, &body, cc_statement_new(c, N_WHILE, loc, cond, block, 0));
	uint32_t out = cc_node_rvalue(c, result);
	if (id == BI_CLZG && (bits & 63))
		out = cc_binary_build(c, LEX_TOKEN_MINUS, out, cc_node_integer_new(c, limbs * 64 - bits, cc_type_basic(TY_INT), loc), loc);
	if (fb) {
		uint32_t zero = cc_binary_build(c, LEX_TOKEN_EQUAL_EQUAL, cc_node_rvalue(c, result), cc_node_integer_new(c, limbs * 64, cc_type_basic(TY_INT), loc), loc);
		out = cc_conditional_build(c, zero, cc_node_rvalue(c, fb), out, loc);
	}
	cc_lowering_statement_append(c, &body, out, loc);
	block = cc_statement_new(c, N_BLOCK, loc, 0, cc_id_list_freeze(c, &body), body.n);
	out = cc_node_new(c, N_STMTEXPR, cc_type_basic(TY_INT), loc);
	AST_NODE(out)->a = block;
	return out;
}

/* Generic bit counts use the operand's precision, without integer promotion.
 * Save effectful operands before selecting a half or the zero fallback; both
 * arguments to a builtin call are evaluated even when the fallback is unused. */
CC_PRIVATE uint32_t cc_builtin_bit_count_generic(cc *c, int id, uint32_t loc)
{
	cc_id_list args;
	uint32_t x, r, pre, fbpre = 0, fb = 0;
	qty xt;
	unsigned bits, width;

	cc_builtin_arguments_parse(c, &args);
	if (args.n < 1 || args.n > (id == BI_POPCOUNTG ? 1u : 2u)) {
		cc_builtin_arity_check(c, loc, id == BI_POPCOUNTG ? 1u : args.n ? 2u : 1u, &args);
		return cc_node_integer_new(c, 0, cc_type_basic(TY_INT), loc);
	}
	x = cc_conversion_decay(c, args.v[0]);
	xt = QUNQ(AST_NODE(x)->ty);
	if (!cc_type_is_integer(c, xt) || cc_type_is_signed(c, xt)) {
		cc_report_error(c, AST_NODE(x)->loc, "1st argument must be a scalar unsigned integer type");
		return cc_node_integer_new(c, 0, cc_type_basic(TY_INT), loc);
	}
	bits = AST_TYPE_KIND(c, xt) == TY_BOOL ? 1 : cc_type_integer_bits(c, xt);
	width = bits > 64 ? 128 : bits > 32 ? 64 : 32;
	if (args.n == 2) {
		uint32_t y = cc_conversion_decay(c, args.v[1]);
		qty ft = cc_node_promoted_type(c, y);
		if (AST_TYPE_KIND(c, ft) != TY_INT || AST_TYPE(c, ft)->n) {
			cc_report_error(c, AST_NODE(y)->loc, "2nd argument must have type int after integer promotion");
			return cc_node_integer_new(c, 0, cc_type_basic(TY_INT), loc);
		}
		fb = cc_conversion_apply(c, y, cc_type_basic(TY_INT));
	}
	if (bits > 128)
		return cc_builtin_bit_count_bitint(c, id, x, fb, bits, loc);
	x = cc_lowering_once(c, x, &pre);
	if (fb) fb = cc_lowering_once(c, fb, &fbpre);
	if (width == 128) {
		uint32_t w = cc_conversion_apply(c, x, cc_type_basic(TY_UINT128));
		uint32_t lo = cc_conversion_apply(c, w, cc_type_basic(TY_ULLONG));
		uint32_t hi = cc_conversion_apply(c, cc_binary_build(c, LEX_TOKEN_GREATER_GREATER, w,
		                             cc_node_integer_new(c, 64, cc_type_basic(TY_INT), loc), loc), cc_type_basic(TY_ULLONG));
		uint32_t low = cc_builtin_bit_count_fixed(c, id, lo, true, loc);
		uint32_t high = cc_builtin_bit_count_fixed(c, id, hi, true, loc);
		if (id == BI_POPCOUNTG)
			r = cc_binary_build(c, LEX_TOKEN_PLUS, low, high, loc);
		else {
			uint32_t first = id == BI_CLZG ? hi : lo;
			uint32_t count = id == BI_CLZG ? high : low;
			uint32_t other = id == BI_CLZG ? low : high;
			uint32_t cond = cc_binary_build(c, LEX_TOKEN_EXCLAMATION_EQUAL, first, cc_node_integer_new(c, 0, cc_type_basic(TY_ULLONG), loc), loc);
			r = cc_conditional_build(c, cond, count,
			                cc_binary_build(c, LEX_TOKEN_PLUS, cc_node_integer_new(c, 64, cc_type_basic(TY_INT), loc), other, loc), loc);
		}
	} else {
		uint32_t w = cc_conversion_apply(c, x, cc_type_basic(width == 64 ? TY_ULLONG : TY_UINT));
		r = cc_builtin_bit_count_fixed(c, id, w, width == 64, loc);
	}
	if (id == BI_CLZG && bits < width)
		r = cc_binary_build(c, LEX_TOKEN_MINUS, r, cc_node_integer_new(c, width - bits, cc_type_basic(TY_INT), loc), loc);
	if (args.n == 2)
		r = cc_conditional_build(c, cc_binary_build(c, LEX_TOKEN_EXCLAMATION_EQUAL, x, cc_node_integer_new(c, 0, xt, loc), loc), r, fb, loc);
	return cc_lowering_sequence(c, pre, cc_lowering_sequence(c, fbpre, r));
}

/* Generic rotations preserve the first operand's type and evaluate each
 * operand once. Normalize the count before narrowing it: the caller may pass
 * a 64/128-bit count even when rotating a small integer. */
CC_PRIVATE uint32_t cc_builtin_rotate_build(cc *c, const cc_builtin *b, uint32_t loc)
{
	cc_id_list args;
	uint32_t prex, precount, pren;

	cc_builtin_arguments_parse(c, &args);
	if (args.n != 2) {
		cc_builtin_arity_check(c, loc, 2, &args);
		return cc_node_error_new(c, loc);
	}
	args.v[0] = cc_conversion_decay(c, args.v[0]);
	args.v[1] = cc_conversion_decay(c, args.v[1]);
	qty t = QUNQ(AST_NODE(args.v[0])->ty), ct = AST_NODE(args.v[1])->ty;
	if (!cc_type_is_integer(c, t) || cc_type_is_signed(c, t)) {
		cc_report_error(c, AST_NODE(args.v[0])->loc, "rotation operand must have unsigned integer type");
		return cc_node_error_new(c, loc);
	}
	if (!cc_type_is_integer(c, ct)) {
		cc_report_error(c, AST_NODE(args.v[1])->loc, "rotation count must have integer type");
		return cc_node_error_new(c, loc);
	}
	unsigned bits = AST_TYPE_KIND(c, t) == TY_BOOL ? 1 : cc_type_integer_bits(c, t);
	qty work = bits > 128 ? t : cc_type_basic(bits <= 32 ? TY_UINT : bits <= 64 ? TY_ULLONG : TY_UINT128);
	unsigned countbits = cc_type_integer_bits(c, ct);
	ct = countbits > 128 ? cc_type_unsigned(c, ct) : cc_type_basic(countbits <= 32 ? TY_UINT : countbits <= 64 ? TY_ULLONG : TY_UINT128);
	uint32_t x = cc_lowering_once(c, cc_conversion_apply(c, cc_node_rvalue(c, args.v[0]), work), &prex);
	uint32_t count = cc_lowering_once(c, cc_conversion_apply(c, cc_node_rvalue(c, args.v[1]), ct), &precount);
	uint32_t n = cc_binary_build(c, LEX_TOKEN_PERCENT, cc_node_rvalue(c, count), cc_node_integer_new(c, bits, ct, loc), loc);
	n = cc_lowering_once(c, cc_conversion_apply(c, n, cc_type_basic(TY_UINT)), &pren);
	uint32_t reverse = cc_binary_build(c, LEX_TOKEN_MINUS, cc_node_integer_new(c, bits, cc_type_basic(TY_UINT), loc), cc_node_rvalue(c, n), loc);
	reverse = cc_binary_build(c, LEX_TOKEN_PERCENT, reverse, cc_node_integer_new(c, bits, cc_type_basic(TY_UINT), loc), loc);
	bool left = b->id == BI_STDC_ROTL;
	uint32_t a = cc_binary_build(c, left ? LEX_TOKEN_LESS_LESS : LEX_TOKEN_GREATER_GREATER, cc_node_rvalue(c, x), cc_node_rvalue(c, n), loc);
	uint32_t z = cc_binary_build(c, left ? LEX_TOKEN_GREATER_GREATER : LEX_TOKEN_LESS_LESS, cc_node_rvalue(c, x), reverse, loc);
	uint32_t value = cc_conversion_apply(c, cc_binary_build(c, LEX_TOKEN_PIPE, a, z, loc), t);
	return cc_lowering_sequence(c, prex, cc_lowering_sequence(c, precount, cc_lowering_sequence(c, pren, value)));
}
