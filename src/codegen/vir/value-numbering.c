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
	uint64_t h = (uint64_t)x->opcode * 0x9e3779b97f4a7c15ull;

	h = (h ^ x->type ^ (uint64_t)x->flags << 8) * 0xff51afd7ed558ccdull;
	h = (h ^ x->a ^ (uint64_t)x->b << 32) * 0xc4ceb9fe1a85ec53ull;
	h = (h ^ x->c ^ (uint64_t)x->generation << 32) * 0x9e3779b97f4a7c15ull;
	for (uint32_t i = (uint32_t)(h >> 40) & S->value_table_mask;; i = (i + 1) & S->value_table_mask) {
		virbuild_value_entry *t = &S->value_table[i];

		if (!t->value || (t->opcode == x->opcode && t->type == x->type && t->flags == x->flags && t->a == x->a &&
			      t->b == x->b && t->c == x->c && t->generation == x->generation))
			return t;
	}
}

/* one pre-IR value per constant (the walker makes one per use) */
static uint32_t virbuild_constant_canonical(virbuild_context *V, virbuild_simplify_state *S, uint32_t v)
{
	const vir_insn *in = &V->insns[v].insn;
	uint64_t h;

	if (V->insns[v].block != VIRBUILD_BLOCK_NONE || in->op == VIR_OP_BPARAM)
		return v;
	h = ((uint64_t)in->op << 8 | in->type) * 0x9e3779b97f4a7c15ull;
	h = (h ^ in->a ^ (uint64_t)in->b << 32) * 0xff51afd7ed558ccdull;
	h = (h ^ in->c) * 0xc4ceb9fe1a85ec53ull;
	for (uint32_t i = (uint32_t)(h >> 40) & S->constant_table_mask;; i = (i + 1) & S->constant_table_mask) {
		uint32_t k = S->constant_table[i];

		if (!k) {
			if (2 * ++S->constant_entry_count > S->constant_table_mask)
				return v;	/* (full: v as it is) */
			S->constant_table[i] = v + 1;
			return v;
		}
		k--;
		if (V->insns[k].insn.op == in->op && V->insns[k].insn.type == in->type && V->insns[k].insn.a == in->a &&
		    V->insns[k].insn.b == in->b && V->insns[k].insn.c == in->c)
			return k;
	}
}

/* the block's instructions, rewritten and numbered (S->ht holds what its dominators computed) */
VPRIV void virbuild_block_values_number(ecx *e, virbuild_simplify_state *S_, uint32_t b)
{
	virbuild_context *V = e->vw;
	virbuild_simplify_state *S = S_;

	for (uint32_t q = V->blocks[b].first_insn; q; q = V->insns[q].next) {
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
			if (r < S->constant_start && V->insns[r].block != VIRBUILD_BLOCK_NONE)
				S->use_counts[r] += S->use_counts[q];
			VIRBUILD_FOR_EACH_OPERAND(V, q, o, virbuild_use_count_decrement(V, S, *o));
			S->use_counts[q] = 0;
			V->forward[q] = r;
			continue;
		}
		op = V->insns[q].insn.op;
		eff = vir_opcode_table[op].effects;
		if (virbuild_promote_store_is_dead(V, q))
			continue;
		if (eff & (VIR_EFFECT_W | VIR_EFFECT_C | VIR_EFFECT_A) || op == VIR_OP_BARRIER || op == VIR_OP_VA_ARG || op == VIR_OP_VA_START)
			S->memory_generation = ++S->block_generations;
		if (op == VIR_OP_STORE) {
			vir_insn in = V->insns[q].insn;
			uint32_t vt = V->insns[in.b].insn.type;

			if (!(in.flags & VIR_INSN_FLAG_VOL) && vir_type_size(vt) && 1u << VIR_INSN_FLAG_WIDTH_LOG2(in.flags) == vir_type_size(vt)) {
				key = (virbuild_value_entry){ VIR_OP_LOAD, vt, VIR_INSN_FLAG_WIDTH_LOG2(in.flags), in.a, 0, 0, S->memory_generation, 0 };
				t = virbuild_value_table_lookup(S, &key);
				if (!t->value)
					S->table_log[S->table_log_count++] = (uint32_t)(t - S->value_table);
				*t = key;
				t->value = in.b;
			}
			if (!(in.flags & VIR_INSN_FLAG_VOL) && (vt == VIR_TYPE_I32 || vt == VIR_TYPE_I64)) {	/* (any load of its width) */
				key = (virbuild_value_entry){ VIR_OP_STORE, 0, VIR_INSN_FLAG_WIDTH_LOG2(in.flags), in.a, 0, 0, S->memory_generation, 0 };
				t = virbuild_value_table_lookup(S, &key);
				if (!t->value)
					S->table_log[S->table_log_count++] = (uint32_t)(t - S->value_table);
				*t = key;
				t->value = in.b;
			}
			continue;
		}
		if (op == VIR_OP_LOAD) {
			vir_insn in = V->insns[q].insn;

			if (in.flags & VIR_INSN_FLAG_VOL)
				continue;
			key = (virbuild_value_entry){ VIR_OP_LOAD, in.type, in.flags & (3 | VIR_INSN_FLAG_MS), in.a, 0, 0, S->memory_generation, 0 };
			if ((in.type == VIR_TYPE_I32 || in.type == VIR_TYPE_I64) && !virbuild_value_table_lookup(S, &key)->value) {
				/* the value a store of this width left: truncated or extended to the load's */
				virbuild_value_entry sk = { VIR_OP_STORE, 0, VIR_INSN_FLAG_WIDTH_LOG2(in.flags), in.a, 0, 0, S->memory_generation, 0 };
				uint32_t v = virbuild_value_table_lookup(S, &sk)->value, w = VIR_INSN_FLAG_WIDTH_LOG2(in.flags), vt;

				if (v) {
					v = virbuild_value_resolve(V, v);
					vt = V->insns[v].insn.type;
					if (in.type == VIR_TYPE_I32 && vt == VIR_TYPE_I64 && w == 2)
						V->insns[q].insn = (vir_insn){ VIR_OP_TRUNC, VIR_TYPE_I32, 0, v, 0, 0 };
					else if (w < 3 && vir_type_size(vt) <= vir_type_size(in.type))
						V->insns[q].insn = (vir_insn){ VIR_OP_EXT, (uint8_t)in.type, (uint16_t)(w | (in.flags & VIR_INSN_FLAG_MS)), v, 0, 0 };
					else
						v = 0;
					if (v) {
						virbuild_use_count_decrement(V, S, in.a), virbuild_use_count_increment(V, S, v);
						goto again;
					}
				}
			}
		} else if ((eff & ~VIR_EFFECT_UF) == VIR_EFFECT_P && op != VIR_OP_BPARAM && op != VIR_OP_PARAM && op != VIR_OP_PROJ) {
			vir_insn in = V->insns[q].insn;

			if (virbuild_opcode_is_commutative(op) && in.a > in.b)
				key = (virbuild_value_entry){ op, in.type, in.flags, in.b, in.a, in.c, S->block, 0 };
			else
				key = (virbuild_value_entry){ op, in.type, in.flags, in.a, in.b, in.c, S->block, 0 };
		} else
			continue;
		t = virbuild_value_table_lookup(S, &key);
		if (op == VIR_OP_LOAD && !t->value && q < S->constant_start && S->positions[q]) {
			virbuild_address A;
			bool ok;

			virbuild_address_decompose(V, V->insns[q].insn.a, &A);
			if (A.off > -VIRBUILD_FAR_LOAD_OFFSET_MAX && A.off < VIRBUILD_FAR_LOAD_OFFSET_MAX) {
				virbuild_value_entry *g;

				/* this memory state's entry first: the two lookups may meet in one empty slot */
				S->table_log[S->table_log_count++] = (uint32_t)(t - S->value_table);
				*t = key;
				t->value = q;
				g = virbuild_far_load_find(V, S, q, &A, &ok);
				if (ok)
					t = g;	/* (q's own entry stays: it is forwarded to the same value) */
				else if (!g->value) {
					S->table_log[S->table_log_count++] = (uint32_t)(g - S->value_table);
					g->value = q;
					continue;
				} else
					continue;
			}
		}
		if (t->value && op == VIR_OP_LOAD && V->insns[virbuild_value_resolve(V, t->value)].insn.type != V->insns[q].insn.type) {
			/* the same bytes, loaded as the other integer type (a truncation retyped it) */
			vir_insn in = V->insns[q].insn;
			uint32_t v = virbuild_value_resolve(V, t->value), vt = V->insns[v].insn.type;

			if (in.type == VIR_TYPE_I32 && vt == VIR_TYPE_I64)
				V->insns[q].insn = (vir_insn){ VIR_OP_TRUNC, VIR_TYPE_I32, 0, v, 0, 0 };
			else if (in.type == VIR_TYPE_I64 && vt == VIR_TYPE_I32 && VIR_INSN_FLAG_WIDTH_LOG2(in.flags) < 3)
				V->insns[q].insn = (vir_insn){ VIR_OP_EXT, VIR_TYPE_I64, (uint16_t)(in.flags & (3 | VIR_INSN_FLAG_MS)), v, 0, 0 };
			else
				v = 0;
			if (v) {
				virbuild_use_count_decrement(V, S, in.a), virbuild_use_count_increment(V, S, v);
				goto again;
			}
		}
		if (t->value && V->insns[virbuild_value_resolve(V, t->value)].insn.type == V->insns[q].insn.type) {
			r = virbuild_value_resolve(V, t->value);
			if (r < S->constant_start && V->insns[r].block != VIRBUILD_BLOCK_NONE)
				S->use_counts[r] += S->use_counts[q];
			VIRBUILD_FOR_EACH_OPERAND(V, q, o, virbuild_use_count_decrement(V, S, *o));
			S->use_counts[q] = 0;
			V->forward[q] = r;
			continue;
		}
		if (!t->value) {
			S->table_log[S->table_log_count++] = (uint32_t)(t - S->value_table);
			*t = key;
			t->value = q;
		}
	}
}
