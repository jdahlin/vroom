/* x86-64 lowering of VIR: register names and sizes, constants, the allocator's edits, edge copies, labels and jumps. */

#include "lowering-internal.h"

VPRIV void *vx_grow(void *p, uint32_t *cap, uint32_t n, size_t size)
{
	if (n <= *cap) return p;
	*cap = n + n / 2 + 16;
	p = realloc(p, (size_t)*cap * size);
	if (!p) abort();
	return p;
}

VPRIV int vx_reg(uint8_t r)
{
	return r == VR_SCR0 ? R11 : r == VR_SCR1 ? XMM15 : VR_CLASS(r) ? XMM0 + VR_NUM(r) : VR_NUM(r);
}

VPRIV int vx_size(uint32_t t) { return vir_isvec(t) ? 16 : t == VT_I32 || t == VT_F32 ? 4 : 8; }

VPRIV int64_t vx_bits(const vir_ins *i) { return (int64_t)((uint64_t)i->c << 32 | i->b); }

VPRIV void vx_raw(vx *L, const char *s)
{
	rec *r = x64_put(L->e, X_RAW);
	r->off = L->e->raw.n;
	r->ref = strlen(s);
	vtext_string_append(&L->e->raw, s);
}

VPRIV void vx_simd(vx *L, const char *op, int d, int a, int b)
{
	char s[160];
	/* A three-address noncommutative operation may overwrite its second source. */
	int out = d == b && d != a ? (XMM0 + 14) : d;
	if (out != a) x64_op3(L->e, X_MOV, 16, out, a, XNOREG);
	snprintf(s, sizeof s, "\t%s\t%%xmm%d, %%xmm%d\n", op, b - XMM0, out - XMM0);
	vx_raw(L, s);
	if (out != d) x64_op3(L->e, X_MOV, 16, d, out, XNOREG);
}

VPRIV void vx_mat(vx *L, uint8_t dst, uint32_t v)
{
	const vir_ins *i = &L->f->ins[v];
	int d = vx_reg(dst);
	if (i->op == VO_FCONST) {
		x64_movi(L->e, L->gtmp, vx_bits(i));
		x64_op3(L->e, X_MOV, vx_size(i->ty), d, L->gtmp, XNOREG);
	} else if (i->op == VO_ICONST) {
		x64_movi(L->e, d, vx_bits(i));
	} else if (i->op == VO_SYMADDR && sym_needs_got(L->e->c, i->a)) {
		rec *r = x64_op3(L->e, X_GOT, 8, d, XNOREG, XNOREG);
		r->ref = i->a;
		if (vx_bits(i)) x64_op3(L->e, X_ADDI, 8, d, d, XNOREG)->imm = vx_bits(i);
	} else {
		lval a = { .k = i->op == VO_SLOTADDR ? L_FRAME : L_SYM,
			.off = vx_bits(i) + (i->op == VO_SLOTADDR ? L->slot[i->a] : 0), .ref = i->a };
		x64_mem(L->e, X_LEA, 8, d, &a, false);
	}
}

VPRIV void vx_memslot(vx *L, bool load, uint8_t reg, uint32_t s)
{
	lval a = { .k = L_FRAME, .off = L->spill[s] };
	x64_mem(L->e, load ? X_LD : X_ST, L->o->ssz[s], vx_reg(reg), &a, false);
}

VPRIV void vx_edit(vx *L, const vir_edit *x)
{
	switch (x->kind) {
	case VE_REMAT: vx_mat(L, x->dst, x->v); break;
	case VE_RELOAD: vx_memslot(L, true, x->dst, L->o->slot[x->v]); break;
	case VE_STORE: vx_memslot(L, false, x->src, L->o->slot[x->v]); break;
	case VE_MOVE: x64_mov(L->e, vx_reg(x->dst), vx_reg(x->src)); break;
	default: abort(); /* no swapped/tied operands in vx_fixed */
	}
}

VPRIV void vx_edge(vx *L, uint32_t b, uint32_t k)
{
	uint32_t g = L->o->eidx[L->o->soff[b] + k];
	if (g == VIR_NOBLK) return;
	vx_edge_moves(L, g);
}

/* edge g's moves */
VPRIV void vx_edge_moves(vx *L, uint32_t g)
{
	const vir_aedge *ed = &L->o->edge[g];
	for (uint32_t j = 0; j < ed->n; j++) {
		const vir_emove *m = &L->o->move[ed->first + j];
		bool ds = m->dst & VL_SLOT, ss = m->src & VL_SLOT;
		uint32_t d = m->dst & ~VL_SLOT, s = m->src & ~VL_SLOT;
		uint8_t tmp = vir_rclass(L->f->ins[m->v].ty) ? VR_MAKE(1, 14) : L->gtmp;
		if (m->kind == VE_REMAT) {
			vx_mat(L, ds ? tmp : d, m->v);
			if (ds) vx_memslot(L, false, tmp, d);
		} else if (m->kind == VE_XCHG) {
			/* Exchanges are register-only; use the second reserved scratch. */
			x64_mov(L->e, vx_reg(tmp), vx_reg(d));
			x64_mov(L->e, vx_reg(d), vx_reg(s));
			x64_mov(L->e, vx_reg(s), vx_reg(tmp));
		} else if (ds && ss) {
			vx_memslot(L, true, tmp, s);
			vx_memslot(L, false, tmp, d);
		} else if (ds) vx_memslot(L, false, s, d);
		else if (ss) vx_memslot(L, true, d, s);
		else x64_mov(L->e, vx_reg(d), vx_reg(s));
	}
}

/* Put edge copies out of line. Branches without copies name their real block. */
VPRIV uint32_t vx_target(vx *L,uint32_t b,uint32_t k,uint32_t target)
{
	uint32_t g=L->o->eidx[L->o->soff[b]+k];
	if(g==VIR_NOBLK || !L->o->edge[g].n)return L->label[L->chase[target]];
	if(!L->edgelab[g])L->edgelab[g]=wk_newlabel(L->e);
	return L->edgelab[g];
}

VPRIV uint32_t vx_next(const vx *L)
{
	if (!L->fallok)
		return VIR_NOBLK;
	uint32_t p = L->position[L->at], nb = L->f->nblk;

	while (++p < nb && L->order[p] && L->chase[L->order[p]] != L->order[p])
		;
	return p < nb ? L->order[p] : VIR_NOBLK;
}

VPRIV void vx_goto(vx *L, uint32_t b, uint32_t target)
{
	uint32_t t = L->chase[target], next = vx_next(L);

	if (vir_layout_duplicate(L->f, L->lp, L->rotation, b, t, next, L->dup, !L->e->E->noopt)) {
		L->dup++;
		virbuild_lower_block(&L->drv, t);
		L->dup--;
		return;
	}
	if (t != next)
		x64_jump(L->e, L->label[t]);
}
