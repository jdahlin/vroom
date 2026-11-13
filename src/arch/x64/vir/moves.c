/* x86-64 lowering of VIR: register names and sizes, constants, the allocator's edits, edge copies, labels and jumps. */

#include "lowering-internal.h"

VPRIV void *x64_vir_array_grow(void *p, uint32_t *cap, uint32_t n, size_t size)
{
	if (n <= *cap) return p;
	*cap = n + n / 2 + 16;
	p = realloc(p, (size_t)*cap * size);
	if (!p) abort();
	return p;
}

VPRIV int x64_vir_register_number(uint8_t r)
{
	return r == VIR_REGISTER_SCRATCH0 ? R11 : r == VIR_REGISTER_SCRATCH1 ? XMM15 : VIR_REGISTER_CLASS(r) ? XMM0 + VIR_REGISTER_NUMBER(r) : VIR_REGISTER_NUMBER(r);
}

VPRIV int x64_vir_type_size(uint32_t t) { return vir_type_is_vector(t) ? 16 : t == VIR_TYPE_I32 || t == VIR_TYPE_F32 ? 4 : 8; }

VPRIV int64_t x64_vir_constant_bits(const vir_insn *i) { return (int64_t)((uint64_t)i->c << 32 | i->b); }

VPRIV void x64_vir_text_append(x64_vir_context *L, const char *s)
{
	rec *r = x64_put(L->e, X_RAW);
	r->off = L->e->raw.n;
	r->ref = strlen(s);
	vtext_string_append(&L->e->raw, s);
}

VPRIV void x64_vir_simd_emit(x64_vir_context *L, const char *op, int d, int a, int b)
{
	char s[160];
	/* A three-address noncommutative operation may overwrite its second source. */
	int out = d == b && d != a ? (XMM0 + 14) : d;
	if (out != a) x64_op3(L->e, X_MOV, 16, out, a, XNOREG);
	snprintf(s, sizeof s, "\t%s\t%%xmm%d, %%xmm%d\n", op, b - XMM0, out - XMM0);
	x64_vir_text_append(L, s);
	if (out != d) x64_op3(L->e, X_MOV, 16, d, out, XNOREG);
}

VPRIV void x64_vir_constant_materialize(x64_vir_context *L, uint8_t dst, uint32_t v)
{
	const vir_insn *i = &L->f->insns[v];
	int d = x64_vir_register_number(dst);
	if (i->op == VIR_OP_FCONST) {
		x64_movi(L->e, L->gtmp, x64_vir_constant_bits(i));
		x64_op3(L->e, X_MOV, x64_vir_type_size(i->type), d, L->gtmp, XNOREG);
	} else if (i->op == VIR_OP_ICONST) {
		x64_movi(L->e, d, x64_vir_constant_bits(i));
	} else if (i->op == VIR_OP_SYMADDR && sym_needs_got(L->e->c, i->a)) {
		rec *r = x64_op3(L->e, X_GOT, 8, d, XNOREG, XNOREG);
		r->ref = i->a;
		if (x64_vir_constant_bits(i)) x64_op3(L->e, X_ADDI, 8, d, d, XNOREG)->imm = x64_vir_constant_bits(i);
	} else {
		lval a = { .k = i->op == VIR_OP_SLOTADDR ? L_FRAME : L_SYM,
			.off = x64_vir_constant_bits(i) + (i->op == VIR_OP_SLOTADDR ? L->slot[i->a] : 0), .ref = i->a };
		x64_mem(L->e, X_LEA, 8, d, &a, false);
	}
}

VPRIV void x64_vir_slot_access(x64_vir_context *L, bool load, uint8_t reg, uint32_t s)
{
	lval a = { .k = L_FRAME, .off = L->spill[s] };
	x64_mem(L->e, load ? X_LD : X_ST, L->o->slot_sizes[s], x64_vir_register_number(reg), &a, false);
}

VPRIV void vx_edit(x64_vir_context *L, const vir_edit *x)
{
	switch (x->kind) {
	case VIR_EDIT_REMATERIALIZE: x64_vir_constant_materialize(L, x->dst, x->value); break;
	case VIR_EDIT_RELOAD: x64_vir_slot_access(L, true, x->dst, L->o->slot[x->value]); break;
	case VIR_EDIT_STORE: x64_vir_slot_access(L, false, x->src, L->o->slot[x->value]); break;
	case VIR_EDIT_MOVE: x64_mov(L->e, x64_vir_register_number(x->dst), x64_vir_register_number(x->src)); break;
	default: abort(); /* no swapped/tied operands in x64_vir_fixed_registers_get */
	}
}

VPRIV void x64_vir_edge_emit(x64_vir_context *L, uint32_t b, uint32_t k)
{
	uint32_t g = L->o->edge_indices[L->o->slot_offsets[b] + k];
	if (g == VIR_BLOCK_NONE) return;
	vx_edge_moves(L, g);
}

/* edge g's moves */
VPRIV void vx_edge_moves(x64_vir_context *L, uint32_t g)
{
	const vir_alloc_edge *ed = &L->o->edge[g];
	for (uint32_t j = 0; j < ed->count; j++) {
		const vir_edge_move *m = &L->o->move[ed->first + j];
		bool ds = m->dst & VIR_LOCATION_SLOT, ss = m->src & VIR_LOCATION_SLOT;
		uint32_t d = m->dst & ~VIR_LOCATION_SLOT, s = m->src & ~VIR_LOCATION_SLOT;
		uint8_t tmp = vir_register_class(L->f->insns[m->value].type) ? VIR_REGISTER_MAKE(1, 14) : L->gtmp;
		if (m->kind == VIR_EDIT_REMATERIALIZE) {
			x64_vir_constant_materialize(L, ds ? tmp : d, m->value);
			if (ds) x64_vir_slot_access(L, false, tmp, d);
		} else if (m->kind == VIR_EDIT_EXCHANGE) {
			/* Exchanges are register-only; use the second reserved scratch. */
			x64_mov(L->e, x64_vir_register_number(tmp), x64_vir_register_number(d));
			x64_mov(L->e, x64_vir_register_number(d), x64_vir_register_number(s));
			x64_mov(L->e, x64_vir_register_number(s), x64_vir_register_number(tmp));
		} else if (ds && ss) {
			x64_vir_slot_access(L, true, tmp, s);
			x64_vir_slot_access(L, false, tmp, d);
		} else if (ds) x64_vir_slot_access(L, false, s, d);
		else if (ss) x64_vir_slot_access(L, true, d, s);
		else x64_mov(L->e, x64_vir_register_number(d), x64_vir_register_number(s));
	}
}

/* Put edge copies out of line. Branches without copies name their real block. */
VPRIV uint32_t x64_vir_edge_target(x64_vir_context *L,uint32_t b,uint32_t k,uint32_t target)
{
	uint32_t g=L->o->edge_indices[L->o->slot_offsets[b]+k];
	if(g==VIR_BLOCK_NONE || !L->o->edge[g].count)return L->label[L->chase[target]];
	if(!L->edgelab[g])L->edgelab[g]=wk_newlabel(L->e);
	return L->edgelab[g];
}

VPRIV uint32_t x64_vir_block_next(const x64_vir_context *L)
{
	if (!L->fallok)
		return VIR_BLOCK_NONE;
	uint32_t p = L->position[L->at], nb = L->f->block_count;

	while (++p < nb && L->order[p] && L->chase[L->order[p]] != L->order[p])
		;
	return p < nb ? L->order[p] : VIR_BLOCK_NONE;
}

VPRIV void x64_vir_goto_emit(x64_vir_context *L, uint32_t b, uint32_t target)
{
	uint32_t t = L->chase[target], next = x64_vir_block_next(L);

	if (vir_layout_duplicate(L->f, L->lp, L->rotation, b, t, next, L->dup, !L->e->E->noopt)) {
		L->dup++;
		virbuild_lower_block(&L->drv, t);
		L->dup--;
		return;
	}
	if (t != next)
		x64_jump(L->e, L->label[t]);
}
