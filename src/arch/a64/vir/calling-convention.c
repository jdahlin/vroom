/* arm64 lowering of VIR: the ABI (AAPCS64, Apple's), the fixed operands of calls, parameters and returns, and the registers offered to the allocator. */

#include "lowering-internal.h"

/* where each argument of signature w goes (AAPCS64; Apple: unnamed on the stack, named
 * packed); returns the count, *stk the bytes of stack arguments */
VPRIV uint32_t a64_vir_arguments_assign(bool darwin, const uint32_t *w, a64_vir_argument *out, uint32_t max, int32_t *stk)
{
	uint32_t ng = VIR_SIGNATURE_GROUP_COUNT(w[0]), ngr = 0, nfr = 0;
	int32_t s = 0;

	for (uint32_t g = 0; g < ng; g++) {
		uint32_t w0 = w[3 + 2 * g], w1 = w[4 + 2 * g], pc = VIR_GROUP_CLASS(w0, 0), size = VIR_GROUP_SIZE(w1);
		a64_vir_argument a = { VIR_REGISTER_NONE, 8, 0 };

		if (!size || size > 8)
			size = 8;
		if (!(darwin && (VIR_GROUP_FLAGS(w0) & VIR_GROUP_UNNAMED))) {
			if (pc == VIR_CLASS_INT && ngr < 8)
				a.reg = VIR_REGISTER_MAKE(0, ngr++);
			else if (pc != VIR_CLASS_INT && nfr < 8)
				a.reg = VIR_REGISTER_MAKE(1, nfr++);
		}
		if (a.reg == VIR_REGISTER_NONE) {
			uint32_t al = darwin && !(VIR_GROUP_FLAGS(w0) & VIR_GROUP_UNNAMED) ? size : 8;

			s = (int32_t)(((uint32_t)s + al - 1) & ~(al - 1));
			a.stk = s;
			a.size = (uint8_t)(al == 8 ? 8 : size);
			s += (int32_t)a.size;
		} else {
			a.size = (uint8_t)size;
		}
		if (g < max)
			out[g] = a;
	}
	*stk = s;
	return ng;
}

static uint8_t a64_vir_return_register(uint32_t ty) { return ty == VIR_TYPE_F32 || ty == VIR_TYPE_F64 ? VIR_REGISTER_MAKE(1, 0) : VIR_REGISTER_MAKE(0, 0); }

#define A64_VIR_CALLER_SAVED_INTEGER ((1ull << 16) - 1)				/* x0-x15 */

#define A64_VIR_ALLOCATABLE_INTEGER (A64_VIR_CALLER_SAVED_INTEGER | ((1ull << 29) - (1ull << 19)))	/* ... and x19-x28 */

#define A64_VIR_ALLOCATABLE_FLOAT (((1ull << 8) - 1) | ((1ull << 31) - (1ull << 16)))	/* v0-v7, v16-v30 */

#define A64_VIR_CALLEE_SAVED_FLOAT_LOW 0xff00ull						/* v8-v15: a call keeps their low 64 bits */

VPRIV bool a64_vir_insn_is_big_memory(const vir_function *f, const vir_insn *in)
{
	uint64_t n = a64_vir_value_is_constant(f, in->c) ? (uint64_t)a64_vir_constant_bits(&f->insns[in->c]) : ~0ull;

	return in->op == VIR_OP_MEMCPY ? n > 128 : n > 256;
}

static void a64_vir_fixed_registers_get(const vir_function *f, uint32_t v, vir_register_constraint *x, void *ctx)
{
	const vir_insn *in = &f->insns[v];
	bool darwin = ctx != NULL;
	a64_vir_argument arg[VIR_REGISTER_CONSTRAINT_MAX];
	int32_t stk;

	switch (in->op) {
	case VIR_OP_PARAM: {
		uint32_t ng = VIR_SIGNATURE_GROUP_COUNT(f->extra[f->signature]);
		a64_vir_argument *a = ng <= A64_VIR_OPERAND_MAX ? __builtin_alloca(sizeof(a64_vir_argument) * ng) : NULL;

		if (!a || in->a >= ng)
			return;
		a64_vir_arguments_assign(darwin, &f->extra[f->signature], a, ng, &stk);
		x->result = a[in->a].reg;
		return;
	}
	case VIR_OP_CALL: {
		uint32_t n = a64_vir_arguments_assign(darwin, &f->extra[in->b], arg, VIR_REGISTER_CONSTRAINT_MAX - 1, &stk);

		x->operand_count = (uint8_t)(n + 1 < VIR_REGISTER_CONSTRAINT_MAX ? n + 1 : VIR_REGISTER_CONSTRAINT_MAX);
		for (uint32_t i = 1; i < x->operand_count; i++) {
			x->operands[i] = arg[i - 1].reg;
			if (arg[i - 1].reg == VIR_REGISTER_NONE)
				x->mem |= 1u << i;
		}
		x->flags |= VIR_CONSTRAINT_FLAG_REST_MEMORY;
		if (f->insns[in->a].op == VIR_OP_SYMADDR && !f->insns[in->a].b && !f->insns[in->a].c)
			x->mem |= 1;
		x->clobbered[0] = A64_VIR_CALLER_SAVED_INTEGER;
		x->clobbered[1] = A64_VIR_ALLOCATABLE_FLOAT;
		if (in->type != VIR_TYPE_VOID && in->type != VIR_TYPE_MULTI)
			x->result = a64_vir_return_register(in->type);
		return;
	}
	case VIR_OP_RET:
		if (f->extra[in->c]) {
			x->operand_count = 1;
			x->operands[0] = a64_vir_return_register(f->insns[f->extra[in->c + 1]].type);
		}
		return;
	case VIR_OP_MEMCPY: case VIR_OP_MEMSET:
		if (a64_vir_insn_is_big_memory(f, in)) {
			x->operand_count = 3;
			x->operands[0] = VIR_REGISTER_MAKE(0, 0), x->operands[1] = VIR_REGISTER_MAKE(0, 1), x->operands[2] = VIR_REGISTER_MAKE(0, 2);
			x->clobbered[0] = A64_VIR_CALLER_SAVED_INTEGER;
			x->clobbered[1] = A64_VIR_ALLOCATABLE_FLOAT;
			return;
		}
		if (f->insns[in->a].op == VIR_OP_SLOTADDR)
			x->mem |= 1;
		if (in->op == VIR_OP_MEMCPY && f->insns[in->b].op == VIR_OP_SLOTADDR)
			x->mem |= 2;
		if (in->op == VIR_OP_MEMSET && a64_vir_value_is_zero(f, in->b))
			x->mem |= 2;
		x->mem |= 4;
		return;
	case VIR_OP_LOAD: case VIR_OP_VA_START:
		if (a64_vir_value_is_memory_address(f, in->a))
			x->mem |= 1;
		return;
	case VIR_OP_STORE:
		if (a64_vir_value_is_memory_address(f, in->a))
			x->mem |= 1;
		if (a64_vir_value_is_zero(f, in->b))
			x->mem |= 2;
		return;
	case VIR_OP_SELECT:	/* (csel reads zero from wzr) */
		if (in->type != VIR_TYPE_F32 && in->type != VIR_TYPE_F64) {
			if (a64_vir_value_is_zero(f, in->b))
				x->mem |= 2;
			if (a64_vir_value_is_zero(f, in->c))
				x->mem |= 4;
		}
		return;
	case VIR_OP_AND: case VIR_OP_OR: case VIR_OP_XOR:	/* (a record's logical immediate must be encodable) */
		if (a64_vir_value_is_integer_constant(f, in->b) && logimm_ok(a64_vir_constant_logical_immediate(&f->insns[in->b]), in->type != VIR_TYPE_I32))
			x->mem |= 2;
		return;
	case VIR_OP_ICMP:
		if (a64_vir_value_is_integer_constant(f, in->b))
			x->mem |= 2;
		else if (a64_vir_value_is_integer_constant(f, in->a))
			x->mem |= 1;	/* (compared the other way round) */
		return;
	case VIR_OP_ADD: case VIR_OP_SUB: case VIR_OP_PTRADD:
	case VIR_OP_SHL: case VIR_OP_LSHR: case VIR_OP_ASHR:
		if (a64_vir_value_is_integer_constant(f, in->b))
			x->mem |= 2;
		return;
	case VIR_OP_MUL:	/* by a power of two: a shift; by a constant fmulk folds: next to the madd */
		if (a64_vir_value_is_integer_constant(f, in->b) && (a64_vir_constant_log2(&f->insns[in->b]) >= 0 || a64_vir_constant_is_multiplier(&f->insns[in->b])))
			x->mem |= 2;
		return;
	case VIR_OP_I2P: case VIR_OP_P2I: case VIR_OP_TRUNC: case VIR_OP_EXT:
		/* copies (or nearly): in the operand's register when it dies here */
		if (in->op != VIR_OP_EXT || VIR_INSN_FLAG_WIDTH_LOG2(in->flags) == 3 || (VIR_INSN_FLAG_WIDTH_LOG2(in->flags) == 2 && !(in->flags & VIR_INSN_FLAG_MS)))
			x->flags |= VIR_CONSTRAINT_FLAG_TIED;
		return;
	default:
		return;
	}
}

static uint8_t a64_vir_operand_fixes[256];

VPRIV void a64_vir_registers_describe(vir_target_registers *r, bool darwin, bool fcs)
{
	static const uint8_t ops[] = { VIR_OP_PARAM, VIR_OP_CALL, VIR_OP_RET, VIR_OP_MEMCPY, VIR_OP_MEMSET, VIR_OP_LOAD, VIR_OP_STORE, VIR_OP_ADD, VIR_OP_SUB,
				       VIR_OP_AND, VIR_OP_OR, VIR_OP_XOR, VIR_OP_ICMP, VIR_OP_PTRADD, VIR_OP_SHL, VIR_OP_LSHR, VIR_OP_ASHR, VIR_OP_MUL,
				       VIR_OP_I2P, VIR_OP_P2I, VIR_OP_TRUNC, VIR_OP_EXT, VIR_OP_SELECT };

	for (size_t i = 0; i < sizeof ops; i++)
		a64_vir_operand_fixes[ops[i]] = 1;
	*r = (vir_target_registers){ 0 };
	r->alloc[0] = A64_VIR_ALLOCATABLE_INTEGER;
	r->alloc[1] = A64_VIR_ALLOCATABLE_FLOAT | (fcs ? A64_VIR_CALLEE_SAVED_FLOAT_LOW : 0);
	r->callee[0] = A64_VIR_ALLOCATABLE_INTEGER & ~A64_VIR_CALLER_SAVED_INTEGER;
	r->callee[1] = fcs ? A64_VIR_CALLEE_SAVED_FLOAT_LOW : 0;
	r->fixed = a64_vir_fixed_registers_get;
	r->context = darwin ? (void *)1 : NULL;
	r->operand_fixes = a64_vir_operand_fixes;
}
