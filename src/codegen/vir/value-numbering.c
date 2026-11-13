/* The simplifier's value numbering: constants, pure instructions and loads of the dominators. */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

/* the table's entry for key x (an empty one when it has none) */
VPRIV virbuild_value_entry *virbuild_value_table_lookup(virbuild_simplify_state *S, const virbuild_value_entry *x)
{
	uint64_t h = (uint64_t)x->op * 0x9e3779b97f4a7c15ull;

	h = (h ^ x->ty ^ (uint64_t)x->fl << 8) * 0xff51afd7ed558ccdull;
	h = (h ^ x->a ^ (uint64_t)x->b << 32) * 0xc4ceb9fe1a85ec53ull;
	h = (h ^ x->c ^ (uint64_t)x->gen << 32) * 0x9e3779b97f4a7c15ull;
	for (uint32_t i = (uint32_t)(h >> 40) & S->hmask;; i = (i + 1) & S->hmask) {
		virbuild_value_entry *t = &S->ht[i];

		if (!t->v || (t->op == x->op && t->ty == x->ty && t->fl == x->fl && t->a == x->a &&
			      t->b == x->b && t->c == x->c && t->gen == x->gen))
			return t;
	}
}

/* one pre-IR value per constant (the walker makes one per use) */
static uint32_t virbuild_constant_canonical(virbuild_context *V, virbuild_simplify_state *S, uint32_t v)
{
	const vir_insn *in = &V->p[v].in;
	uint64_t h;

	if (V->p[v].blk != VIRBUILD_BLOCK_NONE || in->op == VIR_OP_BPARAM)
		return v;
	h = ((uint64_t)in->op << 8 | in->type) * 0x9e3779b97f4a7c15ull;
	h = (h ^ in->a ^ (uint64_t)in->b << 32) * 0xff51afd7ed558ccdull;
	h = (h ^ in->c) * 0xc4ceb9fe1a85ec53ull;
	for (uint32_t i = (uint32_t)(h >> 40) & S->kmask;; i = (i + 1) & S->kmask) {
		uint32_t k = S->kt[i];

		if (!k) {
			if (2 * ++S->nk > S->kmask)
				return v;	/* (full: v as it is) */
			S->kt[i] = v + 1;
			return v;
		}
		k--;
		if (V->p[k].in.op == in->op && V->p[k].in.type == in->type && V->p[k].in.a == in->a &&
		    V->p[k].in.b == in->b && V->p[k].in.c == in->c)
			return k;
	}
}

/* the block's instructions, rewritten and numbered (S->ht holds what its dominators computed) */
VPRIV void virbuild_block_values_number(ecx *e, virbuild_simplify_state *S_, uint32_t b)
{
	virbuild_context *V = e->vw;
	virbuild_simplify_state *S = S_;

	for (uint32_t q = V->b[b].head; q; q = V->p[q].next) {
		uint32_t r, op, eff;
		virbuild_value_entry key, *t;

		if (virbuild_value_resolve(V, q) != q)
			continue;
	again:
		VIRBUILD_FOR_EACH_OPERAND(V, q, o, *o = virbuild_constant_canonical(V, S, virbuild_value_resolve(V, *o)));
		for (int it = 0; virbuild_insn_simplify(e, S, q, &r) && it < 8; it++)
			;	/* (rewritten in place: again) */
		if (r != q) {
			r = virbuild_constant_canonical(V, S, virbuild_value_resolve(V, r));
			if (r < S->n0 && V->p[r].blk != VIRBUILD_BLOCK_NONE)
				S->cnt[r] += S->cnt[q];
			VIRBUILD_FOR_EACH_OPERAND(V, q, o, virbuild_use_count_decrement(V, S, *o));
			S->cnt[q] = 0;
			V->fwd[q] = r;
			continue;
		}
		op = V->p[q].in.op;
		eff = vir_opcode_table[op].effects;
		if (virbuild_promote_store_is_dead(V, q))
			continue;
		if (eff & (VIR_EFFECT_W | VIR_EFFECT_C | VIR_EFFECT_A) || op == VIR_OP_BARRIER || op == VIR_OP_VA_ARG || op == VIR_OP_VA_START)
			S->memgen = ++S->gens;
		if (op == VIR_OP_STORE) {
			vir_insn in = V->p[q].in;
			uint32_t vt = V->p[in.b].in.type;

			if (!(in.flags & VIR_INSN_FLAG_VOL) && vir_type_size(vt) && 1u << VIR_INSN_FLAG_WIDTH_LOG2(in.flags) == vir_type_size(vt)) {
				key = (virbuild_value_entry){ VIR_OP_LOAD, vt, VIR_INSN_FLAG_WIDTH_LOG2(in.flags), in.a, 0, 0, S->memgen, 0 };
				t = virbuild_value_table_lookup(S, &key);
				if (!t->v)
					S->log[S->nlog++] = (uint32_t)(t - S->ht);
				*t = key;
				t->v = in.b;
			}
			if (!(in.flags & VIR_INSN_FLAG_VOL) && (vt == VIR_TYPE_I32 || vt == VIR_TYPE_I64)) {	/* (any load of its width) */
				key = (virbuild_value_entry){ VIR_OP_STORE, 0, VIR_INSN_FLAG_WIDTH_LOG2(in.flags), in.a, 0, 0, S->memgen, 0 };
				t = virbuild_value_table_lookup(S, &key);
				if (!t->v)
					S->log[S->nlog++] = (uint32_t)(t - S->ht);
				*t = key;
				t->v = in.b;
			}
			continue;
		}
		if (op == VIR_OP_LOAD) {
			vir_insn in = V->p[q].in;

			if (in.flags & VIR_INSN_FLAG_VOL)
				continue;
			key = (virbuild_value_entry){ VIR_OP_LOAD, in.type, in.flags & (3 | VIR_INSN_FLAG_MS), in.a, 0, 0, S->memgen, 0 };
			if ((in.type == VIR_TYPE_I32 || in.type == VIR_TYPE_I64) && !virbuild_value_table_lookup(S, &key)->v) {
				/* the value a store of this width left: truncated or extended to the load's */
				virbuild_value_entry sk = { VIR_OP_STORE, 0, VIR_INSN_FLAG_WIDTH_LOG2(in.flags), in.a, 0, 0, S->memgen, 0 };
				uint32_t v = virbuild_value_table_lookup(S, &sk)->v, w = VIR_INSN_FLAG_WIDTH_LOG2(in.flags), vt;

				if (v) {
					v = virbuild_value_resolve(V, v);
					vt = V->p[v].in.type;
					if (in.type == VIR_TYPE_I32 && vt == VIR_TYPE_I64 && w == 2)
						V->p[q].in = (vir_insn){ VIR_OP_TRUNC, VIR_TYPE_I32, 0, v, 0, 0 };
					else if (w < 3 && vir_type_size(vt) <= vir_type_size(in.type))
						V->p[q].in = (vir_insn){ VIR_OP_EXT, (uint8_t)in.type, (uint16_t)(w | (in.flags & VIR_INSN_FLAG_MS)), v, 0, 0 };
					else
						v = 0;
					if (v) {
						virbuild_use_count_decrement(V, S, in.a), virbuild_use_count_increment(V, S, v);
						goto again;
					}
				}
			}
		} else if ((eff & ~VIR_EFFECT_UF) == VIR_EFFECT_P && op != VIR_OP_BPARAM && op != VIR_OP_PARAM && op != VIR_OP_PROJ) {
			vir_insn in = V->p[q].in;

			if (virbuild_opcode_is_commutative(op) && in.a > in.b)
				key = (virbuild_value_entry){ op, in.type, in.flags, in.b, in.a, in.c, S->blk, 0 };
			else
				key = (virbuild_value_entry){ op, in.type, in.flags, in.a, in.b, in.c, S->blk, 0 };
		} else
			continue;
		t = virbuild_value_table_lookup(S, &key);
		if (op == VIR_OP_LOAD && !t->v && q < S->n0 && S->seq[q]) {
			virbuild_address A;
			bool ok;

			virbuild_address_decompose(V, V->p[q].in.a, &A);
			if (A.off > -VIRBUILD_FAR_LOAD_OFFSET_MAX && A.off < VIRBUILD_FAR_LOAD_OFFSET_MAX) {
				virbuild_value_entry *g;

				/* this memory state's entry first: the two lookups may meet in one empty slot */
				S->log[S->nlog++] = (uint32_t)(t - S->ht);
				*t = key;
				t->v = q;
				g = virbuild_far_load_find(V, S, q, &A, &ok);
				if (ok)
					t = g;	/* (q's own entry stays: it is forwarded to the same value) */
				else if (!g->v) {
					S->log[S->nlog++] = (uint32_t)(g - S->ht);
					g->v = q;
					continue;
				} else
					continue;
			}
		}
		if (t->v && op == VIR_OP_LOAD && V->p[virbuild_value_resolve(V, t->v)].in.type != V->p[q].in.type) {
			/* the same bytes, loaded as the other integer type (a truncation retyped it) */
			vir_insn in = V->p[q].in;
			uint32_t v = virbuild_value_resolve(V, t->v), vt = V->p[v].in.type;

			if (in.type == VIR_TYPE_I32 && vt == VIR_TYPE_I64)
				V->p[q].in = (vir_insn){ VIR_OP_TRUNC, VIR_TYPE_I32, 0, v, 0, 0 };
			else if (in.type == VIR_TYPE_I64 && vt == VIR_TYPE_I32 && VIR_INSN_FLAG_WIDTH_LOG2(in.flags) < 3)
				V->p[q].in = (vir_insn){ VIR_OP_EXT, VIR_TYPE_I64, (uint16_t)(in.flags & (3 | VIR_INSN_FLAG_MS)), v, 0, 0 };
			else
				v = 0;
			if (v) {
				virbuild_use_count_decrement(V, S, in.a), virbuild_use_count_increment(V, S, v);
				goto again;
			}
		}
		if (t->v && V->p[virbuild_value_resolve(V, t->v)].in.type == V->p[q].in.type) {
			r = virbuild_value_resolve(V, t->v);
			if (r < S->n0 && V->p[r].blk != VIRBUILD_BLOCK_NONE)
				S->cnt[r] += S->cnt[q];
			VIRBUILD_FOR_EACH_OPERAND(V, q, o, virbuild_use_count_decrement(V, S, *o));
			S->cnt[q] = 0;
			V->fwd[q] = r;
			continue;
		}
		if (!t->v) {
			S->log[S->nlog++] = (uint32_t)(t - S->ht);
			*t = key;
			t->v = q;
		}
	}
}
