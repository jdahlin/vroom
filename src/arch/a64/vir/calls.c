/* arm64 lowering of VIR: calls, sibling calls and parameters. */

#include "lowering-internal.h"

/*
 * Why the function's calls cannot be sibling calls (NULL: those right before a ret can):
 * the callee must not see the frame, so no slot's address may leave the loads, stores
 * and copies that use it (as LLVM's TailCallElim: no escaping alloca).
 */
VPRIV const char *a64_vir_tail_call_reason(a64_vir_context *L, uint8_t *d)
{
	const vir_function *f = L->function;
	bool ch = true;

	memset(d, 0, f->insn_count);
	while (ch) {
		ch = false;
		for (uint32_t b = 0; b < f->block_count; b++)
			for (uint32_t i = f->blocks[b].first; i < f->blocks[b].first + f->blocks[b].insn_count; i++) {
				vir_insn x = f->insns[i];
				vir_operand_iterator it;
				uint32_t *p;

				if (x.op == VIR_OP_ASM || x.op == VIR_OP_ASMGOTO || x.op == VIR_OP_FRAMEADDR || x.op == VIR_OP_RETADDR ||
				    x.op == VIR_OP_VA_START)
					return "asm or the frame's address";
				if (x.op == VIR_OP_CALL && virbuild_call_returns_twice(L->context, &f->insns[x.a]))
					return "a call that returns twice";
				vir_operand_iterator_init(&it, (vir_function *)f, &x, false);
				while ((p = vir_operand_iterator_next(&it))) {
					if (f->insns[*p].op != VIR_OP_SLOTADDR && !d[*p])
						continue;
					if (x.op == VIR_OP_PTRADD && it.pos == 0) {
						ch |= !d[i];
						d[i] = 1;
					} else if (!(((x.op == VIR_OP_LOAD || x.op == VIR_OP_STORE || x.op == VIR_OP_MEMSET) && it.pos == 0) ||
						     (x.op == VIR_OP_MEMCPY && it.pos <= 1))) {
						return "a slot's address escapes";
					}
				}
			}
	}
	return NULL;
}

/* from j in block k: past the instructions that only pass *res on (i2p, p2i; under ret32
 * trunc to i32 and ext.4: the low half is the result, the upper half unspecified), *res
 * following them; the first other instruction */
static uint32_t a64_vir_pass_through_skip(const a64_vir_context *L, const vir_block *k, uint32_t j, uint32_t *res)
{
	const vir_function *f = L->function;

	for (; j < k->first + k->insn_count && f->insns[j].a == *res; j++) {
		const vir_insn *x = &f->insns[j];

		if (!(x->op == VIR_OP_I2P || x->op == VIR_OP_P2I ||
		      (L->ret32 && ((x->op == VIR_OP_TRUNC && x->type == VIR_TYPE_I32) || (x->op == VIR_OP_EXT && VIR_INSN_FLAG_WIDTH_LOG2(x->flags) == 2)))))
			break;
		*res = j;
	}
	return j;
}

/* v without the instructions that only pass a value on (a64_vir_pass_through_skip's): a reader may name the
 * value under them, since the lowering reads through i2p and p2i */
static uint32_t a64_vir_pass_through_value(const a64_vir_context *L, uint32_t v)
{
	const vir_function *f = L->function;

	for (;;) {
		const vir_insn *x = &f->insns[v];

		if (!(x->op == VIR_OP_I2P || x->op == VIR_OP_P2I ||
		      (L->ret32 && ((x->op == VIR_OP_TRUNC && x->type == VIR_TYPE_I32) || (x->op == VIR_OP_EXT && VIR_INSN_FLAG_WIDTH_LOG2(x->flags) == 2)))))
			return v;
		v = x->a;
	}
}

/* the call at i, in b, is a sibling call: what the ret after it returns is its result (or
 * nothing), also through brs to blocks of only what passes the result on */
VPRIV bool a64_vir_call_is_sibling(a64_vir_context *L, uint32_t b, uint32_t i)
{
	const vir_function *f = L->function;
	const vir_block *k = &f->blocks[b];
	const vir_insn *r, *t = &f->insns[f->insns[i].a];
	uint32_t res = i, j;

	if (!L->tail || f->insns[i].type == VIR_TYPE_MULTI ||
	    (t->op == VIR_OP_SYMADDR && RKIND(t->a) == RK_SYM && !em_tail_target_ok(L->context, canon_sym(L->context->c, RIDX(t->a)))))
		return false;
	j = a64_vir_pass_through_skip(L, k, i + 1, &res);
	for (int hop = 0;; hop++) {
		uint32_t q;

		if (j >= k->first + k->insn_count)
			return false;
		r = &f->insns[j];
		if (r->op != VIR_OP_BR || hop == 4)
			break;
		k = &f->blocks[r->a];
		for (q = 0; q < k->parameter_count && a64_vir_pass_through_value(L, f->extra[r->c + q]) != a64_vir_pass_through_value(L, res); q++)
			;
		if (q < k->parameter_count)
			res = k->first + q;	/* (else the ret may name the value itself) */
		j = a64_vir_pass_through_skip(L, k, k->first + k->parameter_count, &res);
	}
	if (r->op != VIR_OP_RET)
		return false;
	return !f->extra[r->c] || (f->extra[r->c] == 1 && a64_vir_pass_through_value(L, f->extra[r->c + 1]) == a64_vir_pass_through_value(L, res));
}

VPRIV void a64_vir_call_lower(a64_vir_context *L, uint32_t b, uint32_t i, const vir_insn *in)
{
	ecx *e = L->context;
	const vir_function *f = L->function;
	uint32_t ng = VIR_SIGNATURE_GROUP_COUNT(f->extra[in->b]);
	a64_vir_argument *a;
	int32_t stk;

	if (ng + 1 > L->operand_count)
		a64_vir_fail(L, VIRBUILD_FAILURE(call_operands));
	a = __builtin_alloca(sizeof(a64_vir_argument) * (ng + 1));
	uint8_t gm = 0;	/* the x registers the arguments are in */

	a64_vir_arguments_assign(DARWIN(e->c), &f->extra[in->b], a, ng, &stk);
	for (uint32_t k = 0; k < ng; k++) {
		uint8_t r = L->operand_registers[1 + k];

		if (a[k].reg != VIR_REGISTER_NONE) {
			if (r != a[k].reg)
				a64_vir_fail(L, VIRBUILD_FAILURE(argument_not_in_register));
			if (!a64_vir_register_is_float(r) && a64_vir_general_register(r) < 8)
				gm |= (uint8_t)(1u << a64_vir_general_register(r));
			else if (!a64_vir_register_is_float(r))
				gm = 0xff;	/* (not an argument register: assume all) */
			continue;
		}
		{
			int sz = log2sz(a[k].size), rs;

			if (r != VIR_REGISTER_NONE && a64_vir_register_is_float(r) && sz >= 2) {
				mem(e, I_STF, sz, 0, a64_vir_vector_register(r), SP, a[k].stack_offset);
				continue;
			}
			if (r != VIR_REGISTER_NONE && a64_vir_register_is_float(r)) {
				word(e, f_out(a[k].size == 8, R_X16, a64_vir_vector_register(r)));
				rs = R_X16;
			} else {
				rs = a64_vir_operand_general_register(L, 1 + k, R_X16);
			}
			mem(e, I_ST, sz, 0, rs, SP, a[k].stack_offset);
		}
	}
	if (!stk && a64_vir_call_is_sibling(L, b, i)) {
		ins *t;

		if (L->allocation->callee[1] && L->operand_registers[0] != VIR_REGISTER_NONE)	/* (the callee's address may be in x16: through x17) */
			L->frame_is_at_risk = true;
		a64_vir_callee_saved_float_save(L, true, L->operand_registers[0] != VIR_REGISTER_NONE ? R_X17 : R_X16);
		t = put(e, I_TAIL);
		t->sh = L->is_before_save_point;

		if (L->operand_registers[0] == VIR_REGISTER_NONE) {
			const vir_insn *k = &f->insns[in->a];

			if (k->op != VIR_OP_SYMADDR || k->b || k->c)
				a64_vir_fail(L, VIRBUILD_FAILURE(indirect_callee_without_register));
			t->cond = 2;
			t->ref = f->insns[in->a].a;
			note_ref(e, t->ref);
		} else {
			t->cond = 3;
			t->rn = (uint8_t)a64_vir_general_register(L->operand_registers[0]);
		}
		e->reach = false;
		L->skip = i + 1;
		return;
	}
	if (stk > e->outmax)
		e->outmax = stk;
	e->calls = true;
	if (L->operand_registers[0] == VIR_REGISTER_NONE) {
		const vir_insn *k = &f->insns[in->a];

		if (k->op != VIR_OP_SYMADDR || k->b || k->c)
			a64_vir_fail(L, VIRBUILD_FAILURE(indirect_callee_without_register));
		note_ref(e, k->a);
		ins *c = put(e, I_BL);

		c->ref = k->a;
		c->cond = CALL_ARGS, c->sh = gm;
	} else {
		ins *c = put(e, I_BLR);

		c->rn = (uint8_t)a64_vir_general_register(L->operand_registers[0]);
		c->cond = CALL_ARGS, c->sh = gm;
	}
}

VPRIV void a64_vir_parameter_lower(a64_vir_context *L, const vir_insn *in, uint8_t res)
{
	ecx *e = L->context;
	const vir_function *f = L->function;
	uint32_t ng = VIR_SIGNATURE_GROUP_COUNT(f->extra[f->signature]);
	a64_vir_argument *a;
	int32_t stk;

	if (ng > A64_VIR_OPERAND_MAX || in->a >= ng)
		a64_vir_fail(L, VIRBUILD_FAILURE(lowering_parameter));
	a = __builtin_alloca(sizeof(a64_vir_argument) * ng);
	a64_vir_arguments_assign(DARWIN(e->c), &f->extra[f->signature], a, ng, &stk);
	if (a[in->a].reg != VIR_REGISTER_NONE) {
		if (res != VIR_REGISTER_NONE && res != a[in->a].reg)
			a64_vir_fail(L, VIRBUILD_FAILURE(parameter_not_in_register));
		return;
	}
	if (res == VIR_REGISTER_NONE)
		return;
	if (a64_vir_register_is_float(res)) {
		mem(e, I_LDF, in->type == VIR_TYPE_F64 ? 3 : 2, 0, a64_vir_vector_register(res), R_FP, 16 + a[in->a].stack_offset);
	} else {
		mem(e, I_LD, log2sz(a[in->a].size), 0, a64_vir_general_register(res), R_FP, 16 + a[in->a].stack_offset);
	}
}
