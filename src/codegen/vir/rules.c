/* The simplifier's rewrite rules: what one instruction becomes, given its operands. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

VPRIV void vw_inc(vwst *V, vwsimp *S, uint32_t v)
{
	if (v < S->n0 && V->p[v].blk != VW_NOBLK)
		S->cnt[v]++;
}

VPRIV void vw_dec(vwst *V, vwsimp *S, uint32_t v)
{
	if (v < S->n0 && V->p[v].blk != VW_NOBLK && S->cnt[v])
		S->cnt[v]--;
}

VPRIV bool vw_kint(const vwst *V, uint32_t v, int64_t *k)
{
	const vir_ins *in = &V->p[v].in;

	if (in->op != VO_ICONST)
		return false;
	*k = in->ty == VT_I32 ? (int64_t)(int32_t)in->b : (int64_t)((uint64_t)in->b | (uint64_t)in->c << 32);
	return true;
}

/* bytes v is zero-extended from (its type's size: nothing known) */
static uint32_t vw_zw(const vwst *V, uint32_t v)
{
	const vir_ins *in = &V->p[v].in;
	uint32_t sz = vir_tysize(in->ty);
	int64_t k;

	switch (in->op) {
	case VO_LOAD:
		return (in->fl & VIF_MS) ? sz : 1u << VIF_W(in->fl) < sz ? 1u << VIF_W(in->fl) : sz;
	case VO_EXT:
		return (in->fl & VIF_MS) ? sz : 1u << VIF_W(in->fl);
	case VO_ICMP: case VO_FCMP:
		return 1;
	case VO_AND:
		if (vw_kint(V, in->b, &k) && k >= 0)
			return k <= 0xff ? 1 : k <= 0xffff ? 2 : k <= 0xffffffffll ? 4 : sz;
		return sz;
	default:
		return sz;
	}
}

/* bytes v is sign-extended from */
static uint32_t vw_sw(const vwst *V, uint32_t v)
{
	const vir_ins *in = &V->p[v].in;
	uint32_t sz = vir_tysize(in->ty), z;

	if ((in->op == VO_LOAD || in->op == VO_EXT) && (in->fl & VIF_MS))
		return 1u << VIF_W(in->fl) < sz ? 1u << VIF_W(in->fl) : sz;
	z = vw_zw(V, v);
	return z < sz ? 2 * z : sz;	/* (zero-extended from z bytes: a signed 2z-byte value) */
}

VPRIV uint32_t vw_undefk(ecx *e, vwsimp *S, uint32_t ty)
{
	if (!S->undef[ty])
		S->undef[ty] = vw_undef(e, ty);
	return S->undef[ty];
}

/* q rewritten in place (true), or the value that replaces it in *r */
VPRIV bool vw_simp1(ecx *e, vwsimp *S, uint32_t q, uint32_t *r)
{
	vwst *V = e->vw;
	vir_ins in = V->p[q].in;
	int64_t k, k2;

	*r = q;
	if (vw_commutes(in.op) && vw_kint(V, in.a, &k) && !vw_kint(V, in.b, &k2)) {
		V->p[q].in.a = in.b, V->p[q].in.b = in.a;	/* the constant second (an immediate) */
		in = V->p[q].in;
	}
	switch (in.op) {
	case VO_I2P: case VO_P2I:	/* a pointer and its address as an integer are the same bits */
		if (V->p[in.a].in.op == (in.op == VO_I2P ? VO_P2I : VO_I2P))
			*r = V->p[in.a].in.a;
		return false;
	case VO_STORE: {	/* only the low bytes go to memory: not their extension, nor a truncation to more */
		vir_ins xi = V->p[in.b].in;

		if (vw_promdead(V, q))
			return false;	/* (it goes: its operands are not counted) */
		if (V->p[in.b].blk != VW_NOBLK && ((xi.op == VO_EXT && VIF_W(xi.fl) >= VIF_W(in.fl)) ||
						   (xi.op == VO_TRUNC && VIF_W(in.fl) <= 2))) {
			vw_dec(V, S, in.b), vw_inc(V, S, xi.a);
			V->p[q].in.b = xi.a;
			return true;
		}
		return false;
	}
	case VO_EXT: {
		uint32_t x = in.a, K = 1u << VIF_W(in.fl), ts = vir_tysize(in.ty);
		bool s = in.fl & VIF_MS;
		vir_ins xi = V->p[x].in;
		uint32_t xs = vir_tysize(xi.ty);

		if (vw_kint(V, x, &k)) {
			uint64_t u = (uint64_t)k & (K == 8 ? ~0ull : (1ull << 8 * K) - 1);

			if (s && K < 8 && (u >> (8 * K - 1)) & 1)
				u |= ~0ull << 8 * K;
			*r = vw_iconst(e, in.ty, (int64_t)u);
			return false;
		}
		if (K >= ts) {	/* (the whole value: a copy or a plain widening) */
			if (xs == ts) {
				*r = x;
				return false;
			}
		}
		if (xs == ts && (s ? vw_sw(V, x) <= K : vw_zw(V, x) <= K)) {
			*r = x;
			return false;
		}
		if (xs == ts && s && vw_zw(V, x) < K) {
			*r = x;
			return false;
		}
		if (xi.op == VO_EXT && vir_tysize(V->p[xi.a].in.ty) <= ts) {
			uint32_t M = 1u << VIF_W(xi.fl);

			if (K <= M) {	/* the low K bytes of x are y's */
				vw_dec(V, S, x), vw_inc(V, S, xi.a);
				V->p[q].in.a = xi.a;
				return true;
			}
			if (s == !!(xi.fl & VIF_MS) || !(xi.fl & VIF_MS)) {	/* x's extension is q's */
				vw_dec(V, S, x), vw_inc(V, S, xi.a);
				V->p[q].in.a = xi.a;
				V->p[q].in.fl = xi.fl;
				return true;
			}
		}
		/* a load only q reads: the load extends */
		if (xi.op == VO_LOAD && !(xi.fl & VIF_VOL) && S->cnt[x] == 1 && (1u << VIF_W(xi.fl)) == K) {
			V->p[x].in.ty = (uint8_t)in.ty;
			V->p[x].in.fl = (uint16_t)((xi.fl & ~VIF_MS) | (s ? VIF_MS : 0));
			*r = x;
			return false;
		}
		if (xi.op == VO_LOAD && !(xi.fl & VIF_VOL) && S->cnt[x] == 1 && (1u << VIF_W(xi.fl)) < K &&
		    (s ? vw_sw(V, x) <= K : vw_zw(V, x) <= K)) {
			V->p[x].in.ty = (uint8_t)in.ty;
			*r = x;
			return false;
		}
		return false;
	}
	case VO_TRUNC: {
		uint32_t x = in.a;
		vir_ins xi = V->p[x].in;

		if (vw_kint(V, x, &k)) {
			*r = vw_iconst(e, VT_I32, (int64_t)(uint32_t)k);
			return false;
		}
		if (xi.op == VO_EXT && V->p[xi.a].in.ty == VT_I32 && VIF_W(xi.fl) >= 2) {
			*r = xi.a;
			return false;
		}
		if (xi.op == VO_EXT && V->p[xi.a].in.ty == VT_I32) {	/* a narrower extension, in i32 */
			vw_dec(V, S, x), vw_inc(V, S, xi.a);
			V->p[q].in = (vir_ins){ VO_EXT, VT_I32, xi.fl, xi.a, 0, 0 };
			return true;
		}
		if (xi.op == VO_EXT && VIF_W(xi.fl) >= 2) {	/* trunc of a widening of the low half */
			vw_dec(V, S, x), vw_inc(V, S, xi.a);
			V->p[q].in.a = xi.a;
			return true;
		}
		if (xi.op == VO_LOAD && !(xi.fl & VIF_VOL) && S->cnt[x] == 1 && VIF_W(xi.fl) <= 2) {
			V->p[x].in.ty = VT_I32;
			if (VIF_W(xi.fl) == 2)
				V->p[x].in.fl &= (uint16_t)~VIF_MS;
			*r = x;
			return false;
		}
		return false;
	}
	case VO_S2F: case VO_U2F:	/* a constant converted: the constant (to nearest, as the instruction) */
		if (vw_kint(V, in.a, &k)) {
			bool w = V->p[in.a].in.ty != VT_I32;
			double d = in.op == VO_S2F ? (w ? (double)k : (double)(int32_t)k) : (w ? (double)(uint64_t)k : (double)(uint32_t)k);
			float f = in.op == VO_S2F ? (w ? (float)k : (float)(int32_t)k) : (w ? (float)(uint64_t)k : (float)(uint32_t)k);
			uint64_t bits;
			uint32_t fb;

			memcpy(&bits, &d, 8), memcpy(&fb, &f, 4);
			*r = in.ty == VT_F32 ? vw_k(e, VO_FCONST, VT_F32, 0, fb, 0) : vw_k(e, VO_FCONST, VT_F64, 0, (uint32_t)bits, (uint32_t)(bits >> 32));
		}
		return false;
	case VO_FEXT: case VO_FTRUNC:
		if (V->p[in.a].in.op == VO_FCONST && V->p[in.a].blk == VW_NOBLK) {
			uint64_t bits = (uint64_t)V->p[in.a].in.b | (uint64_t)V->p[in.a].in.c << 32;
			uint32_t fb = (uint32_t)bits;
			double d;
			float f;

			if (in.op == VO_FEXT) {
				memcpy(&f, &fb, 4);
				d = f;
				if (d != d)
					return false;	/* (a NaN's payload: left to the instruction) */
				memcpy(&bits, &d, 8);
				*r = vw_k(e, VO_FCONST, VT_F64, 0, (uint32_t)bits, (uint32_t)(bits >> 32));
			} else {
				memcpy(&d, &bits, 8);
				f = (float)d;
				if (f != f)
					return false;
				memcpy(&fb, &f, 4);
				*r = vw_k(e, VO_FCONST, VT_F32, 0, fb, 0);
			}
		}
		return false;
	case VO_ADD: case VO_SUB: case VO_OR: case VO_XOR: case VO_SHL: case VO_LSHR: case VO_ASHR:
		if (vw_kint(V, in.b, &k) && !k)
			*r = in.a;
		return false;
	case VO_MUL:
		if (vw_kint(V, in.b, &k) && k == 1)
			*r = in.a;
		return false;
	case VO_AND:
		if (vw_kint(V, in.b, &k) && (k == -1 || (in.ty == VT_I32 && (uint32_t)k == 0xffffffffu)))
			*r = in.a;
		else if (vw_kint(V, in.b, &k) && k > 0 && !((uint64_t)k & ((uint64_t)k + 1)) && vw_zw(V, in.a) < 8 &&
			 (uint64_t)k >= (vw_zw(V, in.a) == 8 ? ~0ull : (1ull << 8 * vw_zw(V, in.a)) - 1))
			*r = in.a;	/* the mask keeps every bit that can be set */
		return false;
	case VO_PTRADD:
		if (!vw_kint(V, in.b, &k))
			return false;
		if (!k) {
			*r = in.a;
			return false;
		}
		if (V->p[in.a].in.op == VO_SLOTADDR && V->p[in.a].blk == VW_NOBLK) {	/* $s+k2 + k: $s+(k2+k) */
			vir_ins a = V->p[in.a].in;	/* (a copy: vw_k can move V->p) */

			k2 = (int64_t)((uint64_t)a.b | (uint64_t)a.c << 32) + k;
			*r = vw_k(e, VO_SLOTADDR, VT_PTR, a.a, (uint32_t)k2, (uint32_t)((uint64_t)k2 >> 32));
			return false;
		}
		if (V->p[in.a].in.op == VO_PTRADD && V->p[in.a].blk != VW_NOBLK && vw_kint(V, V->p[in.a].in.b, &k2)) {
			uint32_t y = V->p[in.a].in.a, c = vw_iconst(e, VT_I64, (int64_t)((uint64_t)k + (uint64_t)k2));

			vw_dec(V, S, in.a), vw_inc(V, S, y);
			V->p[q].in.a = y, V->p[q].in.b = c;
			return true;
		}
		return false;
	default:
		return false;
	}
}

VPRIV bool vw_commutes(uint32_t op)
{
	return op == VO_ADD || op == VO_MUL || op == VO_AND || op == VO_OR || op == VO_XOR ||
	       op == VO_UMULHI || op == VO_SMULHI;
}
