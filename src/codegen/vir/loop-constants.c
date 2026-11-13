/* Loop constants: what a loop computes each time round that costs instructions, made once in front of it (vv_pin). */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "vectorize.h"

/* The loop's constants that cost instructions each time round, made once in front of it (block
 * pre, which only the loop follows): a symbol's address, a float constant, a compare's bound
 * that is no immediate.  Only in a loop without calls (they would need callee-saved registers),
 * at most 6, and only as the operands named here: a call's callee and a block copy's size stay
 * constants.  This is the one place VIR hoists anything (PLAN-vir.md: no LICM in 1.0);
 * -fhoist-loop-constants, on at -O3 */
VPRIV void vv_pin(vvs *S, uint32_t pre)
{
	ecx *e = S->e;
	vwst *V = e->vw;
	uint32_t from[6], to[6], n = 0;

	for (uint32_t i = 0; i < S->nlb; i++)
		for (uint32_t q = V->b[S->lb[i]].head; q; q = V->p[q].next)
			if (!vv_skip(V, q) && vir_optab[V->p[q].in.op].eff & (EF_C | EF_A | EF_T))
				return;
	for (uint32_t i = 0; i < S->nlb; i++)
		for (uint32_t q = V->b[S->lb[i]].head; q; q = V->p[q].next) {
			vir_ins in = V->p[q].in;
			uint32_t m = 0;	/* the operands that may be pinned: bit 0 a, 1 b, 2 c */

			if (q >= S->n0 || vv_skip(V, q))
				continue;
			switch (in.op) {
			case VO_LOAD: case VO_PTRADD: m = 1; break;
			case VO_STORE: m = (in.fl & VIF_VOL) ? 0 : 3; break;
			case VO_FADD: case VO_FSUB: case VO_FMUL: case VO_FDIV: case VO_FCMP: m = 3; break;
			case VO_FMA: m = 7; break;
			case VO_SELECT: m = V->p[q].in.ty == VT_F32 || V->p[q].in.ty == VT_F64 ? 6 : 0; break;
			case VO_ICMP: m = 2; break;
			default: break;
			}
			if (in.op == VO_LOAD && (in.fl & VIF_VOL))
				m = 0;
			for (int k = 0; k < 3; k++) {
				uint32_t *w = k == 0 ? &V->p[q].in.a : k == 1 ? &V->p[q].in.b : &V->p[q].in.c, v, x = 0, j;
				vir_ins c;

				if (!(m >> k & 1))
					continue;
				v = vw_find(V, *w);
				c = V->p[v].in;
				if (V->p[v].blk != VW_NOBLK)
					continue;
				if (c.op == VO_ICONST) {	/* (a compare's bound: add and sub take 12 bits, or 12 shifted by 12) */
					uint64_t u = c.ty == VT_I32 ? (uint64_t)(int64_t)(int32_t)c.b : (uint64_t)c.b | (uint64_t)c.c << 32;
					uint64_t a = (int64_t)u < 0 ? 0 - u : u;

					if (in.op != VO_ICMP || c.ty == VT_PTR || !(a & ~0xfffull) || !(a & ~0xfff000ull))
						continue;
				} else if (c.op == VO_FCONST) {
					if (!c.b && !c.c)
						continue;
				} else if (c.op != VO_SYMADDR) {
					continue;
				}
				for (j = 0; j < n && from[j] != v; j++)
					;
				if (j == n) {
					if (n == 6)
						continue;
					if (c.op == VO_SYMADDR)
						x = vw_ins_in(e, pre, VO_PTRADD, VT_PTR, 0, v, vw_iconst(e, VT_I64, 0), 0);
					else if (c.op == VO_ICONST)
						x = vw_ins_in(e, pre, VO_ADD, c.ty, 0, v, vw_iconst(e, c.ty, 0), 0);
					else if (c.ty == VT_F32)
						x = vw_ins_in(e, pre, VO_BITCAST, VT_F32, 0, vw_iconst(e, VT_I32, (int32_t)c.b), 0, 0);
					else
						x = vw_ins_in(e, pre, VO_BITCAST, VT_F64, 0, vw_iconst(e, VT_I64, (int64_t)((uint64_t)c.b | (uint64_t)c.c << 32)), 0, 0);
					from[n] = v, to[n++] = x;
				}
				w = k == 0 ? &V->p[q].in.a : k == 1 ? &V->p[q].in.b : &V->p[q].in.c;	/* (V->p may have moved) */
				*w = to[j];
			}
		}
}
