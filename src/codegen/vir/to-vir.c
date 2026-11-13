/* The pre-IR emitted as VIR instructions. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* pre-IR value v in VIR (constants made on first use) */
static uint32_t virbuild_to_vir_value(codegen_context *e, uint32_t v)
{
	virbuild_context *V = e->vir_builder;
	virbuild_preir_insn *q;

	v = virbuild_value_resolve(V, v);
	if (V->value_map[v])
		return V->value_map[v];
	q = &V->insns[v];
	if (q->block != VIRBUILD_BLOCK_NONE) {
		V->failure_reason = VIRBUILD_FAILURE(use_before_definition);
		return 0;
	}
	if (q->insn.op == VIR_OP_SLOTADDR) {
		virbuild_preir_slot *s = &V->slots[q->insn.a];

		if (s->final_slot == VIRBUILD_BLOCK_NONE)
			s->final_slot = vir_slot_add(&V->f, s->size, s->align, s->exposed ? 0 : 0, s->off);
		return V->value_map[v] = vir_constant_add(&V->f, VIR_OP_SLOTADDR, VIR_TYPE_PTR, s->final_slot, q->insn.b, q->insn.c);
	}
	return V->value_map[v] = vir_constant_add(&V->f, q->insn.op, q->insn.type, q->insn.a, q->insn.b, q->insn.c);
}

static uint32_t virbuild_to_vir_block(virbuild_context *V, uint32_t b) { return V->blocks[b].final_block; }

VPRIV bool virbuild_to_vir_insn_emit(codegen_context *e, uint32_t i, uint32_t b)
{
	virbuild_context *V = e->vir_builder;
	vir_function *f = &V->f;
	vir_insn in = V->insns[i].insn;
	const vir_opcode_info *oi = &vir_opcode_table[in.op];
	uint32_t w[3] = { in.a, in.b, in.c };

	for (int k = 0; k < 3; k++)
		if (virbuild_operand_kind_is_value(oi->operand_kinds[k]) && !(in.op == VIR_OP_BRIF && k > 0))
			w[k] = virbuild_to_vir_value(e, w[k]);
	switch (in.op) {
	case VIR_OP_CALL: {
		uint32_t n = V->extra[in.c], o;

		w[1] = vir_extra_add(f, &V->extra[in.b], 3 + 2 * VIR_SIGNATURE_GROUP_COUNT(V->extra[in.b]));
		o = vir_extra_add(f, &V->extra[in.c], 1 + n);
		for (uint32_t k = 0; k < n; k++)
			f->extra[o + 1 + k] = virbuild_to_vir_value(e, V->extra[in.c + 1 + k]);
		w[2] = o;
		break;
	}
	case VIR_OP_RET: {
		uint32_t n = V->extra[in.c], o = vir_extra_add(f, &V->extra[in.c], 1 + n);

		for (uint32_t k = 0; k < n; k++)
			f->extra[o + 1 + k] = virbuild_to_vir_value(e, V->extra[in.c + 1 + k]);
		w[2] = o;
		break;
	}
	case VIR_OP_BR: {
		uint32_t t = in.a, n = 0, j = 0, o = 0;

		while (j < V->blocks[t].predecessor_count && V->predecessors[V->blocks[t].predecessor_first + j] != b)
			j++;
		for (uint32_t p = V->blocks[t].first_parameter; p; p = V->insns[p].next)
			if (V->forward[p] == p) {
				uint32_t a = virbuild_to_vir_value(e, V->extra[V->insns[p].insn.b + j]);

				if (!n)
					o = vir_extra_add(f, &a, 1);
				else
					vir_extra_add(f, &a, 1);
				n++;
			}
		w[0] = virbuild_to_vir_block(V, t);
		w[2] = n ? o : 0;
		break;
	}
	case VIR_OP_BRIF:
		w[1] = virbuild_to_vir_block(V, in.b), w[2] = virbuild_to_vir_block(V, in.c);
		break;
	case VIR_OP_IBR: {
		uint32_t n = V->extra[in.c];
		w[2] = vir_extra_add(f, &V->extra[in.c], n + 1);
		for (uint32_t k = 0; k < n; k++)
			f->extra[w[2] + 1 + k] = virbuild_to_vir_block(V, V->extra[in.c + 1 + k]);
		break;
	}
	case VIR_OP_SWITCH: {
		uint32_t n = V->extra[in.c], st = V->insns[in.a].insn.type == VIR_TYPE_I64 ? 5 : 3, o = vir_extra_add(f, &V->extra[in.c], 1 + n * st);

		for (uint32_t k = 0; k < n; k++)
			f->extra[o + 1 + k * st + st - 1] = virbuild_to_vir_block(V, V->extra[in.c + 1 + k * st + st - 1]);
		w[1] = virbuild_to_vir_block(V, in.b), w[2] = o;
		break;
	}
	default:
		break;
	}
	if (V->failure_reason)
		return false;
	{
		uint32_t v = vir_insn_add(f, in.op, in.type, in.flags, w[0], w[1], w[2]);

		if (!v)
			return false;
		V->value_map[i] = v;
	}
	return true;
}
