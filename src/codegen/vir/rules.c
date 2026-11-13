/* The simplifier's rewrite rules: what one instruction becomes, given its operands. */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

VPRIV void virbuild_use_count_increment(virbuild_context *V, virbuild_simplify_state *S, uint32_t v)
{
	if (v < S->n0 && V->p[v].blk != VIRBUILD_BLOCK_NONE)
		S->cnt[v]++;
}

VPRIV void virbuild_use_count_decrement(virbuild_context *V, virbuild_simplify_state *S, uint32_t v)
{
	if (v < S->n0 && V->p[v].blk != VIRBUILD_BLOCK_NONE && S->cnt[v])
		S->cnt[v]--;
}

VPRIV bool virbuild_value_integer_constant(const virbuild_context *V, uint32_t v, int64_t *k)
{
	const vir_insn *in = &V->p[v].in;

	if (in->op != VIR_OP_ICONST)
		return false;
	*k = in->type == VIR_TYPE_I32 ? (int64_t)(int32_t)in->b : (int64_t)((uint64_t)in->b | (uint64_t)in->c << 32);
	return true;
}

/* bytes v is zero-extended from (its type's size: nothing known) */
static uint32_t virbuild_value_zero_extended_bytes(const virbuild_context *V, uint32_t v)
{
	const vir_insn *in = &V->p[v].in;
	uint32_t sz = vir_type_size(in->type);
	int64_t k;

	switch (in->op) {
	case VIR_OP_LOAD:
		return (in->flags & VIR_INSN_FLAG_MS) ? sz : 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags) < sz ? 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags) : sz;
	case VIR_OP_EXT:
		return (in->flags & VIR_INSN_FLAG_MS) ? sz : 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags);
	case VIR_OP_ICMP: case VIR_OP_FCMP:
		return 1;
	case VIR_OP_AND:
		if (virbuild_value_integer_constant(V, in->b, &k) && k >= 0)
			return k <= 0xff ? 1 : k <= 0xffff ? 2 : k <= 0xffffffffll ? 4 : sz;
		return sz;
	default:
		return sz;
	}
}

/* bytes v is sign-extended from */
static uint32_t virbuild_value_sign_extended_bytes(const virbuild_context *V, uint32_t v)
{
	const vir_insn *in = &V->p[v].in;
	uint32_t sz = vir_type_size(in->type), z;

	if ((in->op == VIR_OP_LOAD || in->op == VIR_OP_EXT) && (in->flags & VIR_INSN_FLAG_MS))
		return 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags) < sz ? 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags) : sz;
	z = virbuild_value_zero_extended_bytes(V, v);
	return z < sz ? 2 * z : sz;	/* (zero-extended from z bytes: a signed 2z-byte value) */
}

VPRIV uint32_t virbuild_simplify_undefined_value(ecx *e, virbuild_simplify_state *S, uint32_t ty)
{
	if (!S->undef[ty])
		S->undef[ty] = virbuild_undefined_value(e, ty);
	return S->undef[ty];
}

/* q rewritten in place (true), or the value that replaces it in *r */
VPRIV bool virbuild_insn_simplify(ecx *e, virbuild_simplify_state *S, uint32_t q, uint32_t *r)
{
	virbuild_context *V = e->vw;
	vir_insn in = V->p[q].in;
	int64_t k, k2;

	*r = q;
	if (virbuild_opcode_is_commutative(in.op) && virbuild_value_integer_constant(V, in.a, &k) && !virbuild_value_integer_constant(V, in.b, &k2)) {
		V->p[q].in.a = in.b, V->p[q].in.b = in.a;	/* the constant second (an immediate) */
		in = V->p[q].in;
	}
	switch (in.op) {
	case VIR_OP_I2P: case VIR_OP_P2I:	/* a pointer and its address as an integer are the same bits */
		if (V->p[in.a].in.op == (in.op == VIR_OP_I2P ? VIR_OP_P2I : VIR_OP_I2P))
			*r = V->p[in.a].in.a;
		return false;
	case VIR_OP_STORE: {	/* only the low bytes go to memory: not their extension, nor a truncation to more */
		vir_insn xi = V->p[in.b].in;

		if (virbuild_promote_store_is_dead(V, q))
			return false;	/* (it goes: its operands are not counted) */
		if (V->p[in.b].blk != VIRBUILD_BLOCK_NONE && ((xi.op == VIR_OP_EXT && VIR_INSN_FLAG_WIDTH_LOG2(xi.flags) >= VIR_INSN_FLAG_WIDTH_LOG2(in.flags)) ||
						   (xi.op == VIR_OP_TRUNC && VIR_INSN_FLAG_WIDTH_LOG2(in.flags) <= 2))) {
			virbuild_use_count_decrement(V, S, in.b), virbuild_use_count_increment(V, S, xi.a);
			V->p[q].in.b = xi.a;
			return true;
		}
		return false;
	}
	case VIR_OP_EXT: {
		uint32_t x = in.a, K = 1u << VIR_INSN_FLAG_WIDTH_LOG2(in.flags), ts = vir_type_size(in.type);
		bool s = in.flags & VIR_INSN_FLAG_MS;
		vir_insn xi = V->p[x].in;
		uint32_t xs = vir_type_size(xi.type);

		if (virbuild_value_integer_constant(V, x, &k)) {
			uint64_t u = (uint64_t)k & (K == 8 ? ~0ull : (1ull << 8 * K) - 1);

			if (s && K < 8 && (u >> (8 * K - 1)) & 1)
				u |= ~0ull << 8 * K;
			*r = virbuild_constant_integer(e, in.type, (int64_t)u);
			return false;
		}
		if (K >= ts) {	/* (the whole value: a copy or a plain widening) */
			if (xs == ts) {
				*r = x;
				return false;
			}
		}
		if (xs == ts && (s ? virbuild_value_sign_extended_bytes(V, x) <= K : virbuild_value_zero_extended_bytes(V, x) <= K)) {
			*r = x;
			return false;
		}
		if (xs == ts && s && virbuild_value_zero_extended_bytes(V, x) < K) {
			*r = x;
			return false;
		}
		if (xi.op == VIR_OP_EXT && vir_type_size(V->p[xi.a].in.type) <= ts) {
			uint32_t M = 1u << VIR_INSN_FLAG_WIDTH_LOG2(xi.flags);

			if (K <= M) {	/* the low K bytes of x are y's */
				virbuild_use_count_decrement(V, S, x), virbuild_use_count_increment(V, S, xi.a);
				V->p[q].in.a = xi.a;
				return true;
			}
			if (s == !!(xi.flags & VIR_INSN_FLAG_MS) || !(xi.flags & VIR_INSN_FLAG_MS)) {	/* x's extension is q's */
				virbuild_use_count_decrement(V, S, x), virbuild_use_count_increment(V, S, xi.a);
				V->p[q].in.a = xi.a;
				V->p[q].in.flags = xi.flags;
				return true;
			}
		}
		/* a load only q reads: the load extends */
		if (xi.op == VIR_OP_LOAD && !(xi.flags & VIR_INSN_FLAG_VOL) && S->cnt[x] == 1 && (1u << VIR_INSN_FLAG_WIDTH_LOG2(xi.flags)) == K) {
			V->p[x].in.type = (uint8_t)in.type;
			V->p[x].in.flags = (uint16_t)((xi.flags & ~VIR_INSN_FLAG_MS) | (s ? VIR_INSN_FLAG_MS : 0));
			*r = x;
			return false;
		}
		if (xi.op == VIR_OP_LOAD && !(xi.flags & VIR_INSN_FLAG_VOL) && S->cnt[x] == 1 && (1u << VIR_INSN_FLAG_WIDTH_LOG2(xi.flags)) < K &&
		    (s ? virbuild_value_sign_extended_bytes(V, x) <= K : virbuild_value_zero_extended_bytes(V, x) <= K)) {
			V->p[x].in.type = (uint8_t)in.type;
			*r = x;
			return false;
		}
		return false;
	}
	case VIR_OP_TRUNC: {
		uint32_t x = in.a;
		vir_insn xi = V->p[x].in;

		if (virbuild_value_integer_constant(V, x, &k)) {
			*r = virbuild_constant_integer(e, VIR_TYPE_I32, (int64_t)(uint32_t)k);
			return false;
		}
		if (xi.op == VIR_OP_EXT && V->p[xi.a].in.type == VIR_TYPE_I32 && VIR_INSN_FLAG_WIDTH_LOG2(xi.flags) >= 2) {
			*r = xi.a;
			return false;
		}
		if (xi.op == VIR_OP_EXT && V->p[xi.a].in.type == VIR_TYPE_I32) {	/* a narrower extension, in i32 */
			virbuild_use_count_decrement(V, S, x), virbuild_use_count_increment(V, S, xi.a);
			V->p[q].in = (vir_insn){ VIR_OP_EXT, VIR_TYPE_I32, xi.flags, xi.a, 0, 0 };
			return true;
		}
		if (xi.op == VIR_OP_EXT && VIR_INSN_FLAG_WIDTH_LOG2(xi.flags) >= 2) {	/* trunc of a widening of the low half */
			virbuild_use_count_decrement(V, S, x), virbuild_use_count_increment(V, S, xi.a);
			V->p[q].in.a = xi.a;
			return true;
		}
		if (xi.op == VIR_OP_LOAD && !(xi.flags & VIR_INSN_FLAG_VOL) && S->cnt[x] == 1 && VIR_INSN_FLAG_WIDTH_LOG2(xi.flags) <= 2) {
			V->p[x].in.type = VIR_TYPE_I32;
			if (VIR_INSN_FLAG_WIDTH_LOG2(xi.flags) == 2)
				V->p[x].in.flags &= (uint16_t)~VIR_INSN_FLAG_MS;
			*r = x;
			return false;
		}
		return false;
	}
	case VIR_OP_S2F: case VIR_OP_U2F:	/* a constant converted: the constant (to nearest, as the instruction) */
		if (virbuild_value_integer_constant(V, in.a, &k)) {
			bool w = V->p[in.a].in.type != VIR_TYPE_I32;
			double d = in.op == VIR_OP_S2F ? (w ? (double)k : (double)(int32_t)k) : (w ? (double)(uint64_t)k : (double)(uint32_t)k);
			float f = in.op == VIR_OP_S2F ? (w ? (float)k : (float)(int32_t)k) : (w ? (float)(uint64_t)k : (float)(uint32_t)k);
			uint64_t bits;
			uint32_t fb;

			memcpy(&bits, &d, 8), memcpy(&fb, &f, 4);
			*r = in.type == VIR_TYPE_F32 ? virbuild_constant_make(e, VIR_OP_FCONST, VIR_TYPE_F32, 0, fb, 0) : virbuild_constant_make(e, VIR_OP_FCONST, VIR_TYPE_F64, 0, (uint32_t)bits, (uint32_t)(bits >> 32));
		}
		return false;
	case VIR_OP_FEXT: case VIR_OP_FTRUNC:
		if (V->p[in.a].in.op == VIR_OP_FCONST && V->p[in.a].blk == VIRBUILD_BLOCK_NONE) {
			uint64_t bits = (uint64_t)V->p[in.a].in.b | (uint64_t)V->p[in.a].in.c << 32;
			uint32_t fb = (uint32_t)bits;
			double d;
			float f;

			if (in.op == VIR_OP_FEXT) {
				memcpy(&f, &fb, 4);
				d = f;
				if (d != d)
					return false;	/* (a NaN's payload: left to the instruction) */
				memcpy(&bits, &d, 8);
				*r = virbuild_constant_make(e, VIR_OP_FCONST, VIR_TYPE_F64, 0, (uint32_t)bits, (uint32_t)(bits >> 32));
			} else {
				memcpy(&d, &bits, 8);
				f = (float)d;
				if (f != f)
					return false;
				memcpy(&fb, &f, 4);
				*r = virbuild_constant_make(e, VIR_OP_FCONST, VIR_TYPE_F32, 0, fb, 0);
			}
		}
		return false;
	case VIR_OP_ADD: case VIR_OP_SUB: case VIR_OP_OR: case VIR_OP_XOR: case VIR_OP_SHL: case VIR_OP_LSHR: case VIR_OP_ASHR:
		if (virbuild_value_integer_constant(V, in.b, &k) && !k)
			*r = in.a;
		return false;
	case VIR_OP_MUL:
		if (virbuild_value_integer_constant(V, in.b, &k) && k == 1)
			*r = in.a;
		return false;
	case VIR_OP_AND:
		if (virbuild_value_integer_constant(V, in.b, &k) && (k == -1 || (in.type == VIR_TYPE_I32 && (uint32_t)k == 0xffffffffu)))
			*r = in.a;
		else if (virbuild_value_integer_constant(V, in.b, &k) && k > 0 && !((uint64_t)k & ((uint64_t)k + 1)) && virbuild_value_zero_extended_bytes(V, in.a) < 8 &&
			 (uint64_t)k >= (virbuild_value_zero_extended_bytes(V, in.a) == 8 ? ~0ull : (1ull << 8 * virbuild_value_zero_extended_bytes(V, in.a)) - 1))
			*r = in.a;	/* the mask keeps every bit that can be set */
		return false;
	case VIR_OP_PTRADD:
		if (!virbuild_value_integer_constant(V, in.b, &k))
			return false;
		if (!k) {
			*r = in.a;
			return false;
		}
		if (V->p[in.a].in.op == VIR_OP_SLOTADDR && V->p[in.a].blk == VIRBUILD_BLOCK_NONE) {	/* $s+k2 + k: $s+(k2+k) */
			vir_insn a = V->p[in.a].in;	/* (a copy: virbuild_constant_make can move V->p) */

			k2 = (int64_t)((uint64_t)a.b | (uint64_t)a.c << 32) + k;
			*r = virbuild_constant_make(e, VIR_OP_SLOTADDR, VIR_TYPE_PTR, a.a, (uint32_t)k2, (uint32_t)((uint64_t)k2 >> 32));
			return false;
		}
		if (V->p[in.a].in.op == VIR_OP_PTRADD && V->p[in.a].blk != VIRBUILD_BLOCK_NONE && virbuild_value_integer_constant(V, V->p[in.a].in.b, &k2)) {
			uint32_t y = V->p[in.a].in.a, c = virbuild_constant_integer(e, VIR_TYPE_I64, (int64_t)((uint64_t)k + (uint64_t)k2));

			virbuild_use_count_decrement(V, S, in.a), virbuild_use_count_increment(V, S, y);
			V->p[q].in.a = y, V->p[q].in.b = c;
			return true;
		}
		return false;
	default:
		return false;
	}
}

VPRIV bool virbuild_opcode_is_commutative(uint32_t op)
{
	return op == VIR_OP_ADD || op == VIR_OP_MUL || op == VIR_OP_AND || op == VIR_OP_OR || op == VIR_OP_XOR ||
	       op == VIR_OP_UMULHI || op == VIR_OP_SMULHI;
}
