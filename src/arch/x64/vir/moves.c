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

/* ARM-compatible probe bits 0..7; bit 8 is a definition store, 10 an edge
 * exchange. Bit 9 only maps ordinary lowering (which may branch).
 * Mapping brackets emitted sites (including
 * layout duplicates), not just the allocator's static edit list. Raw records
 * deliberately stop folds: compare probes to a similarly mapped control. */
VPRIV void vx_mark(vx *L, bool begin, unsigned bit, uint32_t b, uint32_t pos,
		    uint32_t v, uint32_t dst, uint32_t src, uint32_t to)
{
	if (!L->map) return;
	char text[384];
	if (!begin) { vx_raw(L,"\t# vl-end\n"); return; }
	uint32_t slot=v?L->o->slot[v]:VIR_NOBLK;
	uint32_t param=0;
	if(to!=VIR_NOBLK)for(uint32_t j=0;j<L->f->blk[to].nparam;j++) {
		uint32_t q=L->f->blk[to].first+j;
		uint32_t loc=L->o->opr[L->o->boff[to]+j];
		if(loc==VR_NONE && L->o->slot[q]!=VIR_NOBLK)loc=VL_SLOT|L->o->slot[q];
		if(loc==dst){param=q;break;}
	}
	snprintf(text,sizeof text,"\t# vl-site id=%u bit=%u b=%u pos=%u v=%u dst=%u src=%u to=%u param=%u slot=%u off=%lld op=%s ty=%s bits=%llu\n",
		L->site++,bit,b,pos,v,dst,src,to,param,slot,
		(long long)(slot==VIR_NOBLK?0:L->spill[slot]),
		v?vir_optab[L->f->ins[v].op].name:"none",
		v?vir_tyname[L->f->ins[v].ty]:"void",
		(unsigned long long)(v && v<L->f->nconst?vx_bits(&L->f->ins[v]):0));
	vx_raw(L,text);
}
VPRIV void vx_probe(vx *L,unsigned bit)
{
	if (L->probe & (1u<<bit)) vx_raw(L,"\tnop\n");
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
	unsigned bit=x->kind==VE_RELOAD?0:x->kind==VE_REMAT?1:x->kind==VE_MOVE?2:3;
	vx_mark(L,true,bit,L->map_block,x->pos,x->v,x->dst,x->src,VIR_NOBLK);
	switch (x->kind) {
	case VE_REMAT: vx_mat(L, x->dst, x->v); break;
	case VE_RELOAD: vx_memslot(L, true, x->dst, L->o->slot[x->v]); break;
	case VE_STORE: vx_memslot(L, false, x->src, L->o->slot[x->v]); break;
	case VE_MOVE: x64_mov(L->e, vx_reg(x->dst), vx_reg(x->src)); break;
	default: abort(); /* no swapped/tied operands in vx_fixed */
	}
	vx_probe(L,bit);
	vx_mark(L,false,bit,0,0,0,0,0,0);
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
		unsigned bit=m->kind==VE_REMAT?4:m->kind==VE_XCHG?10:ds?5:ss?6:7;
		vx_mark(L,true,bit,ed->from,ed->first+j,m->v,m->dst,m->src,ed->to);
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
		vx_probe(L,bit);
		vx_mark(L,false,bit,0,0,0,0,0,0);
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
