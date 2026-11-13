/* The pre-IR emitted as VIR instructions. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* pre-IR value v in VIR (constants made on first use) */
static uint32_t vw_m(ecx *e, uint32_t v)
{
	vwst *V = e->vw;
	vwp *q;

	v = vw_find(V, v);
	if (V->map[v])
		return V->map[v];
	q = &V->p[v];
	if (q->blk != VW_NOBLK) {
		V->why = VIRFAIL(use_before_definition);
		return 0;
	}
	if (q->in.op == VO_SLOTADDR) {
		vwslot *s = &V->sl[q->in.a];

		if (s->fin == VW_NOBLK)
			s->fin = vir_slot_add(&V->f, s->size, s->align, s->exposed ? 0 : 0, s->off);
		return V->map[v] = vir_const(&V->f, VO_SLOTADDR, VT_PTR, s->fin, q->in.b, q->in.c);
	}
	return V->map[v] = vir_const(&V->f, q->in.op, q->in.ty, q->in.a, q->in.b, q->in.c);
}

static uint32_t vw_fin_blk(vwst *V, uint32_t b) { return V->b[b].fin; }

VPRIV bool vw_emit_ins(ecx *e, uint32_t i, uint32_t b)
{
	vwst *V = e->vw;
	vir_fn *f = &V->f;
	vir_ins in = V->p[i].in;
	const vir_opinfo *oi = &vir_optab[in.op];
	uint32_t w[3] = { in.a, in.b, in.c };

	for (int k = 0; k < 3; k++)
		if (vw_isval(oi->k[k]) && !(in.op == VO_BRIF && k > 0))
			w[k] = vw_m(e, w[k]);
	switch (in.op) {
	case VO_CALL: {
		uint32_t n = V->x[in.c], o;

		w[1] = vir_extra(f, &V->x[in.b], 3 + 2 * VSIG_NGROUPS(V->x[in.b]));
		o = vir_extra(f, &V->x[in.c], 1 + n);
		for (uint32_t k = 0; k < n; k++)
			f->extra[o + 1 + k] = vw_m(e, V->x[in.c + 1 + k]);
		w[2] = o;
		break;
	}
	case VO_RET: {
		uint32_t n = V->x[in.c], o = vir_extra(f, &V->x[in.c], 1 + n);

		for (uint32_t k = 0; k < n; k++)
			f->extra[o + 1 + k] = vw_m(e, V->x[in.c + 1 + k]);
		w[2] = o;
		break;
	}
	case VO_BR: {
		uint32_t t = in.a, n = 0, j = 0, o = 0;

		while (j < V->b[t].npred && V->pr[V->b[t].pred + j] != b)
			j++;
		for (uint32_t p = V->b[t].phead; p; p = V->p[p].next)
			if (V->fwd[p] == p) {
				uint32_t a = vw_m(e, V->x[V->p[p].in.b + j]);

				if (!n)
					o = vir_extra(f, &a, 1);
				else
					vir_extra(f, &a, 1);
				n++;
			}
		w[0] = vw_fin_blk(V, t);
		w[2] = n ? o : 0;
		break;
	}
	case VO_BRIF:
		w[1] = vw_fin_blk(V, in.b), w[2] = vw_fin_blk(V, in.c);
		break;
	case VO_SWITCH: {
		uint32_t n = V->x[in.c], st = V->p[in.a].in.ty == VT_I64 ? 5 : 3, o = vir_extra(f, &V->x[in.c], 1 + n * st);

		for (uint32_t k = 0; k < n; k++)
			f->extra[o + 1 + k * st + st - 1] = vw_fin_blk(V, V->x[in.c + 1 + k * st + st - 1]);
		w[1] = vw_fin_blk(V, in.b), w[2] = o;
		break;
	}
	default:
		break;
	}
	if (V->why)
		return false;
	{
		uint32_t v = vir_ins_add(f, in.op, in.ty, in.fl, w[0], w[1], w[2]);

		if (!v)
			return false;
		V->map[i] = v;
	}
	return true;
}
