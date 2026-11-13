/* The pre-IR emitted as VIR instructions. */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* pre-IR value v in VIR (constants made on first use) */
static uint32_t virbuild_to_vir_value(ecx *e, uint32_t v)
{
	virbuild_context *V = e->vw;
	virbuild_preir_insn *q;

	v = virbuild_value_resolve(V, v);
	if (V->map[v])
		return V->map[v];
	q = &V->p[v];
	if (q->blk != VIRBUILD_BLOCK_NONE) {
		V->why = VIRBUILD_FAILURE(use_before_definition);
		return 0;
	}
	if (q->in.op == VIR_OP_SLOTADDR) {
		virbuild_preir_slot *s = &V->sl[q->in.a];

		if (s->fin == VIRBUILD_BLOCK_NONE)
			s->fin = vir_slot_add(&V->f, s->size, s->align, s->exposed ? 0 : 0, s->off);
		return V->map[v] = vir_constant_add(&V->f, VIR_OP_SLOTADDR, VIR_TYPE_PTR, s->fin, q->in.b, q->in.c);
	}
	return V->map[v] = vir_constant_add(&V->f, q->in.op, q->in.type, q->in.a, q->in.b, q->in.c);
}

static uint32_t virbuild_to_vir_block(virbuild_context *V, uint32_t b) { return V->b[b].fin; }

VPRIV bool virbuild_to_vir_insn_emit(ecx *e, uint32_t i, uint32_t b)
{
	virbuild_context *V = e->vw;
	vir_function *f = &V->f;
	vir_insn in = V->p[i].in;
	const vir_opcode_info *oi = &vir_opcode_table[in.op];
	uint32_t w[3] = { in.a, in.b, in.c };

	for (int k = 0; k < 3; k++)
		if (virbuild_operand_kind_is_value(oi->operand_kinds[k]) && !(in.op == VIR_OP_BRIF && k > 0))
			w[k] = virbuild_to_vir_value(e, w[k]);
	switch (in.op) {
	case VIR_OP_CALL: {
		uint32_t n = V->x[in.c], o;

		w[1] = vir_extra_add(f, &V->x[in.b], 3 + 2 * VIR_SIGNATURE_GROUP_COUNT(V->x[in.b]));
		o = vir_extra_add(f, &V->x[in.c], 1 + n);
		for (uint32_t k = 0; k < n; k++)
			f->extra[o + 1 + k] = virbuild_to_vir_value(e, V->x[in.c + 1 + k]);
		w[2] = o;
		break;
	}
	case VIR_OP_RET: {
		uint32_t n = V->x[in.c], o = vir_extra_add(f, &V->x[in.c], 1 + n);

		for (uint32_t k = 0; k < n; k++)
			f->extra[o + 1 + k] = virbuild_to_vir_value(e, V->x[in.c + 1 + k]);
		w[2] = o;
		break;
	}
	case VIR_OP_BR: {
		uint32_t t = in.a, n = 0, j = 0, o = 0;

		while (j < V->b[t].npred && V->pr[V->b[t].pred + j] != b)
			j++;
		for (uint32_t p = V->b[t].phead; p; p = V->p[p].next)
			if (V->fwd[p] == p) {
				uint32_t a = virbuild_to_vir_value(e, V->x[V->p[p].in.b + j]);

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
	case VIR_OP_SWITCH: {
		uint32_t n = V->x[in.c], st = V->p[in.a].in.type == VIR_TYPE_I64 ? 5 : 3, o = vir_extra_add(f, &V->x[in.c], 1 + n * st);

		for (uint32_t k = 0; k < n; k++)
			f->extra[o + 1 + k * st + st - 1] = virbuild_to_vir_block(V, V->x[in.c + 1 + k * st + st - 1]);
		w[1] = virbuild_to_vir_block(V, in.b), w[2] = o;
		break;
	}
	default:
		break;
	}
	if (V->why)
		return false;
	{
		uint32_t v = vir_insn_add(f, in.op, in.type, in.flags, w[0], w[1], w[2]);

		if (!v)
			return false;
		V->map[i] = v;
	}
	return true;
}
