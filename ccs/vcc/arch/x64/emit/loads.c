/* Repeated loads in a block (emit/rec/loads.h): x86 says which records read
 * and write what.  Volatile accesses carry sh 1 (x64_mem) and are left alone.
 */
#include "x64impl.h"
#include "../../../emit/rec/loads.h"

static void x64_mem_inspect(void *ctx, uint32_t k, em_rec_mem *m)
{
	ecx *e = ctx;
	const rec *i = &e->x64->in[k];
	receffects f;

	if (i->op == 0xff)
		return;
	f = x64_effects(e, i);
	m->write = f.def;
	if (f.br != RB_NONE || (f.fl & (OD_LABEL | OD_BARRIER | OD_CALL))) {
		m->kind = EM_MEM_BOUNDARY;
	} else if (i->op == X_LD || i->op == X_ST) {
		bool st = i->op == X_ST;
		uint8_t r = i->r[st ? 2 : 0];

		if (i->sh) {
			m->kind = st ? EM_MEM_OTHER : EM_MEM_NONE;
		} else if (r >= XMM0) {
			m->kind = st ? EM_MEM_OTHER : EM_MEM_NONE;
		} else {
			m->kind = st ? EM_MEM_STORE : EM_MEM_LOAD;
			m->base = i->r[1], m->sz = i->sz, m->cond = i->cond, m->imm = i->off, m->ref = i->ref, m->reg = r;
			m->forward = st && i->sz == 8 && !i->cond;
		}
	} else if (f.fl & OD_STORE) {
		m->kind = EM_MEM_OTHER;
	}
}

static void x64_mem_reuse(void *ctx, uint32_t k, uint8_t from, uint32_t pos)
{
	rec *i = &((ecx *)ctx)->x64->in[k];
	uint8_t rd = i->r[0];

	(void)pos;
	if (rd == from) {
		i->op = 0xff;
		return;
	}
	*i = (rec){ .op = X_MOV, .sz = 8, .r = { rd, from, XNOREG, XNOREG } };
}

/* after two-address lowering, so the copies are forwarded; the deleted records go */
VPRIV void x64_loads(ecx *e, void (*remap)(void *, uint32_t, uint32_t), void *ctx)
{
	xst *x = e->x64;
	const em_rec_load_scan scan = {
		.records = x->in, .stride = sizeof(rec), .op_offset = offsetof(rec, op), .deleted_op = 0xff,
		.inspect = x64_mem_inspect, .reuse = x64_mem_reuse, .ctx = e,
	};

	uint32_t n = 0;

	if (!x->nin)
		return;
	em_rec_load_reuse(&scan, 0, x->nin - 1);
	for (uint32_t k = 0; k < x->nin; k++) {
		if (remap)
			remap(ctx, k, n);
		if (x->in[k].op != 0xff)
			x->in[n++] = x->in[k];
	}
	if (remap)
		remap(ctx, UINT32_MAX, n);
	x->nin = n;
}
