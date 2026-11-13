/* vcc front end: popcount, clz, ctz and the stdc_ bit operations lowered to ordinary code; see builtin-table.c for where a builtin's other facts live */
#include "ccimpl.h"

/* A fixed-width bit operation used by the generic forms. */
static uint32_t fixed_bitcount(cc *c, int id, uint32_t x, bool wide, uint32_t loc)
{
	vec args;
	int fid = id == BI_CLZG ? (wide ? BI_CLZLL : BI_CLZ) :
	          id == BI_CTZG ? (wide ? BI_CTZLL : BI_CTZ) :
	                         (wide ? BI_POPCOUNTLL : BI_POPCOUNT);
	vinit(&args);
	vpush(c, &args, x);
	return mk_builtin(c, fid, prim(TY_INT), loc, &args);
}

/* A compact limb loop keeps the AST independent of the integer precision.
 * Explicit declarations preserve operand snapshots across loop iterations. */
VPRIV uint32_t bitcount_local(cc *c, vec *body, qty t, uint32_t init, uint32_t loc)
{
	uint32_t sym = sym_new(c, S_VAR, 0, t, loc);
	c->syms[sym].sc = SC_AUTO;
	uint32_t var = mk(c, N_VAR, t, loc);
	N(var)->a = sym;
	N(var)->flags = NF_LVALUE;
	vpush(c, body, mk2(c, N_DECL, prim(TY_VOID), loc, sym, init));
	return var;
}

VPRIV void bitcount_statement(cc *c, vec *body, uint32_t n, uint32_t loc)
{
	vpush(c, body, mkstmt(c, N_EXPR, loc, n, 0, 0));
}

VPRIV uint32_t big_bitcount(cc *c, int id, uint32_t x, uint32_t fallback, unsigned bits, uint32_t loc)
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
		return with_pre(c, fallback, mk_int(c, count, prim(TY_INT), loc));
	}
	vec body, loop, found;
	vinit(&body); vinit(&loop); vinit(&found);
	uint32_t value = bitcount_local(c, &body, QUNQ(N(x)->ty), x, loc);
	uint32_t fb = fallback ? bitcount_local(c, &body, prim(TY_INT), fallback, loc) : 0;
	uint32_t result = bitcount_local(c, &body, prim(TY_INT), mk_int(c, 0, prim(TY_INT), loc), loc);
	uint32_t index = bitcount_local(c, &body, prim(TY_INT), mk_int(c, id == BI_CLZG ? limbs - 1 : 0, prim(TY_INT), loc), loc);
	uint32_t address = mk(c, N_ADDR, ty_ptr(c, N(value)->ty), loc);
	N(address)->a = value;
	address = cast_node(c, address, ty_ptr(c, prim(TY_ULLONG)), CK_NOP);
	uint32_t word = rvalue(c, deref(c, binop(c, LEX_TOKEN_PLUS, address, rvalue(c, index), loc), loc));
	if (bits & 63) {
		uint32_t top = binop(c, LEX_TOKEN_EQUAL_EQUAL, rvalue(c, index), mk_int(c, limbs - 1, prim(TY_INT), loc), loc);
		uint32_t mask = mk_int(c, (i128)(UINT64_MAX >> (64 - (bits & 63))), prim(TY_ULLONG), loc);
		word = conditional(c, top, binop(c, LEX_TOKEN_AMPERSAND, word, mask, loc), word, loc);
	}
	word = bitcount_local(c, &loop, prim(TY_ULLONG), word, loc);
	uint32_t count = fixed_bitcount(c, id, rvalue(c, word), true, loc);
	if (id == BI_POPCOUNTG) {
		bitcount_statement(c, &loop, assign(c, LEX_TOKEN_PLUS, result, count, loc), loc);
	} else {
		bitcount_statement(c, &found, assign(c, LEX_TOKEN_PLUS, result, count, loc), loc);
		vpush(c, &found, mkstmt(c, N_BREAK, loc, 0, 0, 0));
		uint32_t block = mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, &found), found.n);
		vpush(c, &loop, mkstmt(c, N_IF, loc, rvalue(c, word), block, 0));
		bitcount_statement(c, &loop, assign(c, LEX_TOKEN_PLUS, result, mk_int(c, 64, prim(TY_INT), loc), loc), loc);
	}
	bitcount_statement(c, &loop, assign(c, id == BI_CLZG ? LEX_TOKEN_MINUS : LEX_TOKEN_PLUS, index, mk_int(c, 1, prim(TY_INT), loc), loc), loc);
	uint32_t cond = binop(c, id == BI_CLZG ? LEX_TOKEN_GREATER_EQUAL : LEX_TOKEN_LESS, rvalue(c, index),
	                     mk_int(c, id == BI_CLZG ? 0 : limbs, prim(TY_INT), loc), loc);
	uint32_t block = mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, &loop), loop.n);
	vpush(c, &body, mkstmt(c, N_WHILE, loc, cond, block, 0));
	uint32_t out = rvalue(c, result);
	if (id == BI_CLZG && (bits & 63))
		out = binop(c, LEX_TOKEN_MINUS, out, mk_int(c, limbs * 64 - bits, prim(TY_INT), loc), loc);
	if (fb) {
		uint32_t zero = binop(c, LEX_TOKEN_EQUAL_EQUAL, rvalue(c, result), mk_int(c, limbs * 64, prim(TY_INT), loc), loc);
		out = conditional(c, zero, rvalue(c, fb), out, loc);
	}
	bitcount_statement(c, &body, out, loc);
	block = mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, &body), body.n);
	out = mk(c, N_STMTEXPR, prim(TY_INT), loc);
	N(out)->a = block;
	return out;
}

/* Generic bit counts use the operand's precision, without integer promotion.
 * Save effectful operands before selecting a half or the zero fallback; both
 * arguments to a builtin call are evaluated even when the fallback is unused. */
VPRIV uint32_t generic_bitcount(cc *c, int id, uint32_t loc)
{
	vec args;
	uint32_t x, r, pre, fbpre = 0, fb = 0;
	qty xt;
	unsigned bits, width;

	args_list(c, &args);
	if (args.n < 1 || args.n > (id == BI_POPCOUNTG ? 1u : 2u)) {
		arity(c, loc, id == BI_POPCOUNTG ? 1u : args.n ? 2u : 1u, &args);
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	x = decay(c, args.v[0]);
	xt = QUNQ(N(x)->ty);
	if (!is_integer(c, xt) || is_signed(c, xt)) {
		cc_error(c, N(x)->loc, "1st argument must be a scalar unsigned integer type");
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	bits = KIND(c, xt) == TY_BOOL ? 1 : int_bits(c, xt);
	width = bits > 64 ? 128 : bits > 32 ? 64 : 32;
	if (args.n == 2) {
		uint32_t y = decay(c, args.v[1]);
		qty ft = promoted(c, y);
		if (KIND(c, ft) != TY_INT || T(c, ft)->n) {
			cc_error(c, N(y)->loc, "2nd argument must have type int after integer promotion");
			return mk_int(c, 0, prim(TY_INT), loc);
		}
		fb = convert(c, y, prim(TY_INT));
	}
	if (bits > 128)
		return big_bitcount(c, id, x, fb, bits, loc);
	x = cplx_reuse(c, x, &pre);
	if (fb) fb = cplx_reuse(c, fb, &fbpre);
	if (width == 128) {
		uint32_t w = convert(c, x, prim(TY_UINT128));
		uint32_t lo = convert(c, w, prim(TY_ULLONG));
		uint32_t hi = convert(c, binop(c, LEX_TOKEN_GREATER_GREATER, w,
		                             mk_int(c, 64, prim(TY_INT), loc), loc), prim(TY_ULLONG));
		uint32_t low = fixed_bitcount(c, id, lo, true, loc);
		uint32_t high = fixed_bitcount(c, id, hi, true, loc);
		if (id == BI_POPCOUNTG)
			r = binop(c, LEX_TOKEN_PLUS, low, high, loc);
		else {
			uint32_t first = id == BI_CLZG ? hi : lo;
			uint32_t count = id == BI_CLZG ? high : low;
			uint32_t other = id == BI_CLZG ? low : high;
			uint32_t cond = binop(c, LEX_TOKEN_EXCLAMATION_EQUAL, first, mk_int(c, 0, prim(TY_ULLONG), loc), loc);
			r = conditional(c, cond, count,
			                binop(c, LEX_TOKEN_PLUS, mk_int(c, 64, prim(TY_INT), loc), other, loc), loc);
		}
	} else {
		uint32_t w = convert(c, x, prim(width == 64 ? TY_ULLONG : TY_UINT));
		r = fixed_bitcount(c, id, w, width == 64, loc);
	}
	if (id == BI_CLZG && bits < width)
		r = binop(c, LEX_TOKEN_MINUS, r, mk_int(c, width - bits, prim(TY_INT), loc), loc);
	if (args.n == 2)
		r = conditional(c, binop(c, LEX_TOKEN_EXCLAMATION_EQUAL, x, mk_int(c, 0, xt, loc), loc), r, fb, loc);
	return with_pre(c, pre, with_pre(c, fbpre, r));
}

/* Generic rotations preserve the first operand's type and evaluate each
 * operand once. Normalize the count before narrowing it: the caller may pass
 * a 64/128-bit count even when rotating a small integer. */
VPRIV uint32_t stdc_rotate(cc *c, const builtin *b, uint32_t loc)
{
	vec args;
	uint32_t prex, precount, pren;

	args_list(c, &args);
	if (args.n != 2) {
		arity(c, loc, 2, &args);
		return err_node(c, loc);
	}
	args.v[0] = decay(c, args.v[0]);
	args.v[1] = decay(c, args.v[1]);
	qty t = QUNQ(N(args.v[0])->ty), ct = N(args.v[1])->ty;
	if (!is_integer(c, t) || is_signed(c, t)) {
		cc_error(c, N(args.v[0])->loc, "rotation operand must have unsigned integer type");
		return err_node(c, loc);
	}
	if (!is_integer(c, ct)) {
		cc_error(c, N(args.v[1])->loc, "rotation count must have integer type");
		return err_node(c, loc);
	}
	unsigned bits = KIND(c, t) == TY_BOOL ? 1 : int_bits(c, t);
	qty work = bits > 128 ? t : prim(bits <= 32 ? TY_UINT : bits <= 64 ? TY_ULLONG : TY_UINT128);
	unsigned countbits = int_bits(c, ct);
	ct = countbits > 128 ? unsigned_of(c, ct) : prim(countbits <= 32 ? TY_UINT : countbits <= 64 ? TY_ULLONG : TY_UINT128);
	uint32_t x = cplx_reuse(c, convert(c, rvalue(c, args.v[0]), work), &prex);
	uint32_t count = cplx_reuse(c, convert(c, rvalue(c, args.v[1]), ct), &precount);
	uint32_t n = binop(c, LEX_TOKEN_PERCENT, rvalue(c, count), mk_int(c, bits, ct, loc), loc);
	n = cplx_reuse(c, convert(c, n, prim(TY_UINT)), &pren);
	uint32_t reverse = binop(c, LEX_TOKEN_MINUS, mk_int(c, bits, prim(TY_UINT), loc), rvalue(c, n), loc);
	reverse = binop(c, LEX_TOKEN_PERCENT, reverse, mk_int(c, bits, prim(TY_UINT), loc), loc);
	bool left = b->id == BI_STDC_ROTL;
	uint32_t a = binop(c, left ? LEX_TOKEN_LESS_LESS : LEX_TOKEN_GREATER_GREATER, rvalue(c, x), rvalue(c, n), loc);
	uint32_t z = binop(c, left ? LEX_TOKEN_GREATER_GREATER : LEX_TOKEN_LESS_LESS, rvalue(c, x), reverse, loc);
	uint32_t value = convert(c, binop(c, LEX_TOKEN_PIPE, a, z, loc), t);
	return with_pre(c, prex, with_pre(c, precount, with_pre(c, pren, value)));
}
