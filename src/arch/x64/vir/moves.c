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
	record *r = x64_put(L->context, X_RAW);
	r->off = L->context->raw.n;
	r->ref = strlen(s);
	vtext_string_append(&L->context->raw, s);
}

/* ARM-compatible probe bits 0..7; bit 8 is a definition store, 10 an edge
 * exchange. Bit 9 only maps ordinary lowering (which may branch).
 * Mapping brackets emitted sites (including
 * layout duplicates), not just the allocator's static edit list. Raw records
 * deliberately stop folds: compare probes to a similarly mapped control. */
VPRIV void vx_mark(x64_vir_context *L, bool begin, unsigned bit, uint32_t b, uint32_t pos,
		    uint32_t v, uint32_t dst, uint32_t src, uint32_t to)
{
	if (!L->map) return;
	char text[384];
	if (!begin) { x64_vir_text_append(L,"\t# vl-end\n"); return; }
	uint32_t slot=v?L->allocation->slot[v]:VIR_BLOCK_NONE;
	uint32_t param=0;
	if(to!=VIR_BLOCK_NONE)for(uint32_t j=0;j<L->function->blocks[to].parameter_count;j++) {
		uint32_t q=L->function->blocks[to].first+j;
		uint32_t loc=L->allocation->operand_registers[L->allocation->block_offsets[to]+j];
		if(loc==VIR_REGISTER_NONE && L->allocation->slot[q]!=VIR_BLOCK_NONE)loc=VIR_LOCATION_SLOT|L->allocation->slot[q];
		if(loc==dst){param=q;break;}
	}
	snprintf(text,sizeof text,"\t# vl-site id=%u bit=%u b=%u pos=%u v=%u dst=%u src=%u to=%u param=%u slot=%u off=%lld op=%s ty=%s bits=%llu\n",
		L->site++,bit,b,pos,v,dst,src,to,param,slot,
		(long long)(slot==VIR_BLOCK_NONE?0:L->spill[slot]),
		v?vir_opcode_table[L->function->insns[v].op].name:"none",
		v?vir_type_names[L->function->insns[v].type]:"void",
		(unsigned long long)(v && v<L->function->constant_count?x64_vir_constant_bits(&L->function->insns[v]):0));
	x64_vir_text_append(L,text);
}
VPRIV void vx_probe(x64_vir_context *L,unsigned bit)
{
	if (L->probe & (1u<<bit)) x64_vir_text_append(L,"\tnop\n");
}
VPRIV void x64_vir_simd_emit(x64_vir_context *L, const char *op, int d, int a, int b)
{
	char s[160];
	/* A three-address noncommutative operation may overwrite its second source. */
	int out = d == b && d != a ? (XMM0 + 14) : d;
	if (out != a) x64_op3(L->context, X_MOV, 16, out, a, XNOREG);
	snprintf(s, sizeof s, "\t%s\t%%xmm%d, %%xmm%d\n", op, b - XMM0, out - XMM0);
	x64_vir_text_append(L, s);
	if (out != d) x64_op3(L->context, X_MOV, 16, d, out, XNOREG);
}

VPRIV void x64_vir_constant_materialize(x64_vir_context *L, uint8_t dst, uint32_t v)
{
	const vir_insn *i = &L->function->insns[v];
	int d = x64_vir_register_number(dst);
	if (i->op == VIR_OP_FCONST) {
		x64_movi(L->context, L->general_temporary, x64_vir_constant_bits(i));
		x64_op3(L->context, X_MOV, x64_vir_type_size(i->type), d, L->general_temporary, XNOREG);
	} else if (i->op == VIR_OP_ICONST) {
		x64_movi(L->context, d, x64_vir_constant_bits(i));
	} else if (i->op == VIR_OP_SYMADDR && codegen_symbol_needs_got(L->context->c, i->a)) {
		record *r = x64_op3(L->context, X_GOT, 8, d, XNOREG, XNOREG);
		r->ref = i->a;
		if (x64_vir_constant_bits(i)) x64_op3(L->context, X_ADDI, 8, d, d, XNOREG)->imm = x64_vir_constant_bits(i);
	} else {
		codegen_lvalue a = { .k = i->op == VIR_OP_SLOTADDR ? CODEGEN_LVALUE_FRAME : CODEGEN_LVALUE_SYMBOL,
			.off = x64_vir_constant_bits(i) + (i->op == VIR_OP_SLOTADDR ? L->slot[i->a] : 0), .ref = i->a };
		x64_mem(L->context, X_LEA, 8, d, &a, false);
	}
}

VPRIV void x64_vir_slot_access(x64_vir_context *L, bool load, uint8_t reg, uint32_t s)
{
	codegen_lvalue a = { .k = CODEGEN_LVALUE_FRAME, .off = L->spill[s] };
	x64_mem(L->context, load ? X_LD : X_ST, L->allocation->slot_sizes[s], x64_vir_register_number(reg), &a, false);
}

VPRIV void vx_edit(x64_vir_context *L, const vir_edit *x)
{
	unsigned bit=x->kind==VIR_EDIT_RELOAD?0:x->kind==VIR_EDIT_REMATERIALIZE?1:x->kind==VIR_EDIT_MOVE?2:3;
	vx_mark(L,true,bit,L->map_block,x->pos,x->value,x->dst,x->src,VIR_BLOCK_NONE);
	switch (x->kind) {
	case VIR_EDIT_REMATERIALIZE: x64_vir_constant_materialize(L, x->dst, x->value); break;
	case VIR_EDIT_RELOAD: x64_vir_slot_access(L, true, x->dst, L->allocation->slot[x->value]); break;
	case VIR_EDIT_STORE: x64_vir_slot_access(L, false, x->src, L->allocation->slot[x->value]); break;
	case VIR_EDIT_MOVE: x64_mov(L->context, x64_vir_register_number(x->dst), x64_vir_register_number(x->src)); break;
	default: abort(); /* no swapped/tied operands in x64_vir_fixed_registers_get */
	}
	vx_probe(L,bit);
	vx_mark(L,false,bit,0,0,0,0,0,0);
}

VPRIV void x64_vir_edge_emit(x64_vir_context *L, uint32_t b, uint32_t k)
{
	uint32_t g = L->allocation->edge_indices[L->allocation->slot_offsets[b] + k];
	if (g == VIR_BLOCK_NONE) return;
	vx_edge_moves(L, g);
}

/* edge g's moves */
VPRIV void vx_edge_moves(x64_vir_context *L, uint32_t g)
{
	const vir_alloc_edge *ed = &L->allocation->edge[g];
	for (uint32_t j = 0; j < ed->count; j++) {
		const vir_edge_move *m = &L->allocation->move[ed->first + j];
		bool ds = m->dst & VIR_LOCATION_SLOT, ss = m->src & VIR_LOCATION_SLOT;
		uint32_t d = m->dst & ~VIR_LOCATION_SLOT, s = m->src & ~VIR_LOCATION_SLOT;
		uint8_t tmp = vir_register_class(L->function->insns[m->value].type) ? VIR_REGISTER_MAKE(1, 14) : L->general_temporary;
		unsigned bit=m->kind==VIR_EDIT_REMATERIALIZE?4:m->kind==VIR_EDIT_EXCHANGE?10:ds?5:ss?6:7;
		vx_mark(L,true,bit,ed->from,ed->first+j,m->value,m->dst,m->src,ed->to);
		if (m->kind == VIR_EDIT_REMATERIALIZE) {
			x64_vir_constant_materialize(L, ds ? tmp : d, m->value);
			if (ds) x64_vir_slot_access(L, false, tmp, d);
		} else if (m->kind == VIR_EDIT_EXCHANGE) {
			/* Exchanges are register-only; use the second reserved scratch. */
			x64_mov(L->context, x64_vir_register_number(tmp), x64_vir_register_number(d));
			x64_mov(L->context, x64_vir_register_number(d), x64_vir_register_number(s));
			x64_mov(L->context, x64_vir_register_number(s), x64_vir_register_number(tmp));
		} else if (ds && ss) {
			x64_vir_slot_access(L, true, tmp, s);
			x64_vir_slot_access(L, false, tmp, d);
		} else if (ds) x64_vir_slot_access(L, false, s, d);
		else if (ss) x64_vir_slot_access(L, true, d, s);
		else x64_mov(L->context, x64_vir_register_number(d), x64_vir_register_number(s));
		vx_probe(L,bit);
		vx_mark(L,false,bit,0,0,0,0,0,0);
	}
}

/* Put edge copies out of line. Branches without copies name their real block. */
VPRIV uint32_t x64_vir_edge_target(x64_vir_context *L,uint32_t b,uint32_t k,uint32_t target)
{
	uint32_t g=L->allocation->edge_indices[L->allocation->slot_offsets[b]+k];
	if(g==VIR_BLOCK_NONE || !L->allocation->edge[g].count)return L->label[L->chase[target]];
	if(!L->edge_labels[g])L->edge_labels[g]=walk_label_new(L->context);
	return L->edge_labels[g];
}

VPRIV uint32_t x64_vir_block_next(const x64_vir_context *L)
{
	if (!L->may_fall_through)
		return VIR_BLOCK_NONE;
	uint32_t p = L->position[L->block_current], nb = L->function->block_count;

	while (++p < nb && L->order[p] && L->chase[L->order[p]] != L->order[p])
		;
	return p < nb ? L->order[p] : VIR_BLOCK_NONE;
}

VPRIV void x64_vir_goto_emit(x64_vir_context *L, uint32_t b, uint32_t target)
{
	uint32_t t = L->chase[target], next = x64_vir_block_next(L);

	if (vir_layout_duplicate(L->function, L->loops, L->rotations, b, t, next, L->dup, !L->context->E->noopt)) {
		L->dup++;
		virbuild_lower_block(&L->drv, t);
		L->dup--;
		return;
	}
	if (t != next)
		x64_jump(L->context, L->label[t]);
}
