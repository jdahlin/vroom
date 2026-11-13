/* Loop constants: what a loop computes each time round that costs instructions, made once in front of it (virbuild_loop_constants_pin). */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "vectorize.h"

/* The loop's constants that cost instructions each time round, made once in front of it (block
 * pre, which only the loop follows): a symbol's address, a float constant, a compare's bound
 * that is no immediate.  Only in a loop without calls (they would need callee-saved registers),
 * at most 6, and only as the operands named here: a call's callee and a block copy's size stay
 * constants.  This is the one place VIR hoists anything (PLAN-vir.md: no LICM in 1.0);
 * VCC_VIR_VEC=n turns it off */
VPRIV void virbuild_loop_constants_pin(virbuild_vectorize_state *S, uint32_t pre)
{
	ecx *e = S->context;
	virbuild_context *V = e->vw;
	uint32_t from[6], to[6], n = 0;

	for (uint32_t i = 0; i < S->loop_block_count; i++)
		for (uint32_t q = V->blocks[S->loop_blocks[i]].first_insn; q; q = V->insns[q].next)
			if (!virbuild_vectorize_insn_is_skipped(V, q) && vir_opcode_table[V->insns[q].insn.op].effects & (VIR_EFFECT_C | VIR_EFFECT_A | VIR_EFFECT_T))
				return;
	for (uint32_t i = 0; i < S->loop_block_count; i++)
		for (uint32_t q = V->blocks[S->loop_blocks[i]].first_insn; q; q = V->insns[q].next) {
			vir_insn in = V->insns[q].insn;
			uint32_t m = 0;	/* the operands that may be pinned: bit 0 a, 1 b, 2 c */

			if (q >= S->value_count || virbuild_vectorize_insn_is_skipped(V, q))
				continue;
			switch (in.op) {
			case VIR_OP_LOAD: case VIR_OP_PTRADD: m = 1; break;
			case VIR_OP_STORE: m = (in.flags & VIR_INSN_FLAG_VOL) ? 0 : 3; break;
			case VIR_OP_FADD: case VIR_OP_FSUB: case VIR_OP_FMUL: case VIR_OP_FDIV: case VIR_OP_FCMP: m = 3; break;
			case VIR_OP_FMA: m = 7; break;
			case VIR_OP_SELECT: m = V->insns[q].insn.type == VIR_TYPE_F32 || V->insns[q].insn.type == VIR_TYPE_F64 ? 6 : 0; break;
			case VIR_OP_ICMP: m = 2; break;
			default: break;
			}
			if (in.op == VIR_OP_LOAD && (in.flags & VIR_INSN_FLAG_VOL))
				m = 0;
			for (int k = 0; k < 3; k++) {
				uint32_t *w = k == 0 ? &V->insns[q].insn.a : k == 1 ? &V->insns[q].insn.b : &V->insns[q].insn.c, v, x = 0, j;
				vir_insn c;

				if (!(m >> k & 1))
					continue;
				v = virbuild_value_resolve(V, *w);
				c = V->insns[v].insn;
				if (V->insns[v].block != VIRBUILD_BLOCK_NONE)
					continue;
				if (c.op == VIR_OP_ICONST) {	/* (a compare's bound: add and sub take 12 bits, or 12 shifted by 12) */
					uint64_t u = c.type == VIR_TYPE_I32 ? (uint64_t)(int64_t)(int32_t)c.b : (uint64_t)c.b | (uint64_t)c.c << 32;
					uint64_t a = (int64_t)u < 0 ? 0 - u : u;

					if (in.op != VIR_OP_ICMP || c.type == VIR_TYPE_PTR || !(a & ~0xfffull) || !(a & ~0xfff000ull))
						continue;
				} else if (c.op == VIR_OP_FCONST) {
					if (!c.b && !c.c)
						continue;
				} else if (c.op != VIR_OP_SYMADDR) {
					continue;
				}
				for (j = 0; j < n && from[j] != v; j++)
					;
				if (j == n) {
					if (n == 6)
						continue;
					if (c.op == VIR_OP_SYMADDR)
						x = virbuild_block_insn_append(e, pre, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, v, virbuild_constant_integer(e, VIR_TYPE_I64, 0), 0);
					else if (c.op == VIR_OP_ICONST)
						x = virbuild_block_insn_append(e, pre, VIR_OP_ADD, c.type, 0, v, virbuild_constant_integer(e, c.type, 0), 0);
					else if (c.type == VIR_TYPE_F32)
						x = virbuild_block_insn_append(e, pre, VIR_OP_BITCAST, VIR_TYPE_F32, 0, virbuild_constant_integer(e, VIR_TYPE_I32, (int32_t)c.b), 0, 0);
					else
						x = virbuild_block_insn_append(e, pre, VIR_OP_BITCAST, VIR_TYPE_F64, 0, virbuild_constant_integer(e, VIR_TYPE_I64, (int64_t)((uint64_t)c.b | (uint64_t)c.c << 32)), 0, 0);
					from[n] = v, to[n++] = x;
				}
				w = k == 0 ? &V->insns[q].insn.a : k == 1 ? &V->insns[q].insn.b : &V->insns[q].insn.c;	/* (V->p may have moved) */
				*w = to[j];
			}
		}
}
