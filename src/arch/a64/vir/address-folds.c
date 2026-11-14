/* arm64 lowering of VIR: folding an address or an index chain into the instruction that uses it. */

#include "lowering-internal.h"

/* ptradd i (operands collected) with a constant offset, used once, by the next
 * instruction as its address: that adds the offset (its base's register is still
 * there unless an edit before it writes it) */
VPRIV bool a64_vir_pointer_fold(a64_vir_context *L, uint32_t b, uint32_t i)
{
	const vir_function *f = L->function;
	const vir_insn *in = &f->insns[i], *nx = &f->insns[i + 1];
	uint8_t base = L->operand_registers[0];
	int64_t k;

	if (L->operand_registers[1] != VIR_REGISTER_NONE || !a64_vir_value_is_integer_constant(f, in->b) || base == VIR_REGISTER_NONE || L->uses[i] != 1 ||
	    L->allocation->slot[i] != VIR_BLOCK_NONE || i + 1 >= f->blocks[b].first + f->blocks[b].insn_count || nx->a != i ||
	    (nx->op != VIR_OP_LOAD && !(nx->op == VIR_OP_STORE && nx->b != i)) || a64_vir_access_is_vector(f, nx))
		return false;
	k = a64_vir_constant_bits(&f->insns[in->b]);
	if (k < -256 || k > 32760)
		return false;
	for (uint32_t x = L->edit_next; x < L->edit_end && L->allocation->edit[x].pos <= i + 1; x++)
		if (L->allocation->edit[x].pos == i + 1 && L->allocation->edit[x].dst == base)
			return false;
	L->pointer_value = i;
	L->pointer_base = a64_vir_general_register(base);
	L->pointer_offset = k;
	return true;
}

enum { A64_VIR_CHAIN_MEM = 1, A64_VIR_CHAIN_EXT, A64_VIR_CHAIN_LSL, A64_VIR_CHAIN_MADDL };

/* the index operand (b) of c, a ptradd, add or sub of 64 bits, built by the instructions right
 * before c, each used once by the next, that c folds:
 *   A64_VIR_CHAIN_MEM   c a ptradd used once by the next instruction, a load or store through it:
 *            [base, index], the index extended (ext.4s/4u) and scaled by the access size (mul or
 *            shl);
 *   A64_VIR_CHAIN_EXT   a + (ext.4s/4u x << 0-4): add/sub (extended register);
 *   A64_VIR_CHAIN_LSL   a + (x << k): add/sub (shifted register);
 *   A64_VIR_CHAIN_MADDL a + ext.4s/4u x * K (a ptradd or add): smaddl/umaddl with K in a register.
 * The skipped instructions write no register, so x and a are still where they were when no
 * allocator edit comes in between.  x->first: the chain's first instruction, x->xv: x. */
VPRIV bool a64_vir_chain_info_get(const a64_vir_context *L, uint32_t b, uint32_t c, a64_vir_chain *x)
{
	const vir_function *f = L->function;
	const vir_block *k = &f->blocks[b];
	const vir_insn *in = &f->insns[c], *m = &f->insns[c + 1];
	uint32_t j = in->b, lo = k->first + k->parameter_count, end = k->first + k->insn_count, w = 0;
	bool mem, sh = false;
	int kk = 0;

	if ((in->op != VIR_OP_PTRADD && in->op != VIR_OP_ADD && in->op != VIR_OP_SUB) || in->type == VIR_TYPE_I32 || a64_vir_value_is_constant(f, j) ||
	    (a64_vir_value_is_constant(f, in->a) && in->op != VIR_OP_PTRADD))
		return false;
	mem = in->op == VIR_OP_PTRADD && c + 1 < end && m->a == c && a64_vir_value_is_folded(L, c) &&
	      (m->op == VIR_OP_LOAD || (m->op == VIR_OP_STORE && m->b != c)) && !a64_vir_access_is_vector(f, m);
	if (mem)
		w = VIR_INSN_FLAG_WIDTH_LOG2(m->flags);
	*x = (a64_vir_chain){ c, j, mem ? A64_VIR_CHAIN_MEM : 0, 3, 0, 0 };
	if (j + 1 == c && j > lo && a64_vir_value_is_folded(L, j) && !a64_vir_value_is_constant(f, f->insns[j].a) && a64_vir_value_is_integer_constant(f, f->insns[j].b) &&
	    (f->insns[j].op == VIR_OP_MUL || f->insns[j].op == VIR_OP_SHL)) {
		const vir_insn *q = &f->insns[j], *e = &f->insns[q->a];
		int64_t K = a64_vir_constant_integer(&f->insns[q->b]);

		kk = q->op == VIR_OP_SHL ? (int)K : a64_vir_constant_log2(&f->insns[q->b]);
		if (q->op == VIR_OP_SHL && (K < 0 || K > 63))
			kk = -1;
		if (mem ? kk == (int)w && w : kk > 0) {
			x->first = j, x->xv = q->a, sh = true;
			j = q->a;
		} else if (!mem && q->op == VIR_OP_MUL && in->op != VIR_OP_SUB && q->a + 1 == j && q->a >= lo && a64_vir_value_is_folded(L, q->a) &&
			   e->op == VIR_OP_EXT && VIR_INSN_FLAG_WIDTH_LOG2(e->flags) == 2 && e->type == VIR_TYPE_I64 && !a64_vir_value_is_constant(f, e->a) &&
			   ((e->flags & VIR_INSN_FLAG_MS) ? K >= INT32_MIN && K <= INT32_MAX : K >= 0 && K <= UINT32_MAX)) {
			*x = (a64_vir_chain){ q->a, e->a, A64_VIR_CHAIN_MADDL, (e->flags & VIR_INSN_FLAG_MS) ? 6 : 2, 0, K };
			return true;
		}
	}
	/* The shared no-overflow rule also admits shift.i32 -> sext.i64.
	 * Preserve the allocator's lifetimes and fold into the native SXTW form. */
	unsigned signed_scale;
	if(j+1==c && j>lo && a64_vir_value_is_folded(L,j) && a64_vir_value_is_folded(L,j-1) &&
	   vir_sign_extend_shift_match(f,j-1,&signed_scale) && signed_scale<=4 && !a64_vir_value_is_constant(f,f->insns[j-1].a)) {
		bool scaled_mem=mem && signed_scale==w;
		*x=(a64_vir_chain){j-1,f->insns[j-1].a,scaled_mem?A64_VIR_CHAIN_MEM:A64_VIR_CHAIN_EXT,6,
		         scaled_mem?(signed_scale!=0):signed_scale,0};
		return true;
	}
	if (j + 1 == x->first && j >= lo && a64_vir_value_is_folded(L, j) && f->insns[j].op == VIR_OP_EXT && VIR_INSN_FLAG_WIDTH_LOG2(f->insns[j].flags) == 2 &&
	    f->insns[j].type == VIR_TYPE_I64 && !a64_vir_value_is_constant(f, f->insns[j].a) && (mem || kk <= 4)) {
		x->first = j, x->xv = f->insns[j].a, x->opt = f->insns[j].flags & VIR_INSN_FLAG_MS ? 6 : 2;
	}
	x->sh = mem ? sh : (uint8_t)(sh ? kk : 0);
	if (!mem)
		x->kind = x->opt != 3 ? A64_VIR_CHAIN_EXT : sh ? A64_VIR_CHAIN_LSL : 0;
	return x->kind != 0;
}

/* the chain's consumer c: its folded form (A64_VIR_CHAIN_MEM: the next load or store's address) */
static void a64_vir_chain_consumer_act(a64_vir_context *L, uint32_t c, uint8_t res)
{
	codegen_context *e = L->context;
	uint32_t op = L->function->insns[c].op;
	const a64_vir_chain *x = &L->xx;
	int a = a64_vir_operand_general_register(L, 0, R_X16), rd = res == VIR_REGISTER_NONE ? R_X16 : a64_vir_general_register(res), t;	/* (a base in x16: a64_vir_store_lower takes x17) */

	L->chain_consumer = VIR_BLOCK_NONE;
	switch (x->kind) {
	case A64_VIR_CHAIN_MEM:
		L->index_value = c, L->index_base = a, L->index_extension = x->opt, L->index_shift = x->sh;
		return;
	case A64_VIR_CHAIN_EXT: {
		a64_record *n = a64_record_emit(e, op == VIR_OP_SUB ? A64_OP_SUB : A64_OP_ADD);	/* (extended register: cond 8 + option) */

		n->size = 1, n->rd = (uint8_t)rd, n->rn = (uint8_t)a, n->rm = (uint8_t)L->index_register, n->condition = (uint8_t)(8 + x->opt), n->shift = x->sh;
		return;
	}
	case A64_VIR_CHAIN_LSL:
		a64_record_emit_three_registers_shifted(e, op == VIR_OP_SUB ? A64_OP_SUB : A64_OP_ADD, 1, rd, a, L->index_register, 0, x->sh);
		return;
	default:	/* A64_VIR_CHAIN_MADDL */
		t = L->index_register == R_X17 || a == R_X17 ? R_X16 : R_X17;
		a64_record_emit_move_immediate(e, 1, t, x->k);
		a64_record_emit_four_registers(e, x->opt == 6 ? A64_OP_SMADDL : A64_OP_UMADDL, 1, rd, L->index_register, t, a);
		return;
	}
}

/* instruction i (lowered with its operands collected) is in a chain a64_vir_chain_info_get folds: true when it
 * is handled (nothing to emit, or the consumer's folded form).  Decided at the chain's first
 * instruction, where the index's register is known: between it and the consumer only remats and
 * reloads into other registers (a constant operand, the base), none before the load or store */
VPRIV bool a64_vir_chain_fold(a64_vir_context *L, uint32_t b, uint32_t i, uint8_t res)
{
	const vir_function *f = L->function;
	a64_vir_chain x;

	if (L->chain_consumer != VIR_BLOCK_NONE && i > L->chain_first && i <= L->chain_consumer) {
		if (i == L->chain_consumer)
			a64_vir_chain_consumer_act(L, i, res);
		return true;
	}
	for (uint32_t c = i; c <= i + 2 && c < f->blocks[b].first + f->blocks[b].insn_count; c++) {
		uint32_t op = f->insns[c].op, end;
		uint8_t r;

		if (op != VIR_OP_PTRADD && op != VIR_OP_ADD && op != VIR_OP_SUB)
			continue;
		if (!a64_vir_chain_info_get(L, b, c, &x) || x.first != i)
			return false;
		r = L->operand_registers[i == c ? 1 : 0];
		if (r == VIR_REGISTER_NONE || a64_vir_register_is_float(r) || a64_vir_general_register(r) == R_X16 || a64_vir_general_register(r) == R_X17)
			return false;
		end = c + (x.kind == A64_VIR_CHAIN_MEM);
		for (uint32_t q = L->edit_next; q < L->edit_end && L->allocation->edit[q].pos <= end; q++) {	/* (those before are at i or earlier) */
			const vir_edit *d = &L->allocation->edit[q];

			if (d->pos > i && !(d->pos <= c && (d->kind == VIR_EDIT_REMATERIALIZE || d->kind == VIR_EDIT_RELOAD) && d->dst != r))
				return false;
		}
		L->xx = x, L->chain_first = i, L->chain_consumer = c, L->index_register = a64_vir_general_register(r);
		if (i == c)
			a64_vir_chain_consumer_act(L, i, res);
		return true;
	}
	return false;
}
