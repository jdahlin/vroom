/*
 * A synthetic register target for testing the allocator (PLAN-vir.md V4)
 * until V2 gives the real x86-64 one: N integer and M float registers, the
 * top C and D of them callee-saved, and optionally x86-64's constraints.
 *
 *   integer registers r0..r(N-1), float f0..f(M-1) (bytes 64..)
 *   calls: integer arguments in r0..r(min(N-C-1, 6)-1) (one caller-saved
 *          register stays free for the callee's address), float arguments
 *          in f0..f(min(M-D, 8)-1), a group that does not fit goes on the
 *          stack as a whole; the SRET pointer takes the first integer
 *          register; results in r0, r1 and f0, f1; every caller-saved
 *          register is clobbered; `param` and `ret` use the same registers
 *   x86:   two-address integer and float arithmetic (the result in the
 *          first operand's register), shift counts in r2, division with the
 *          dividend and quotient in r0, the remainder in r1, r1 written
 *          before the divisor is read; switch writes r0 after reading the selector
 *          and r1 before it
 *   both:  a constant second operand of an integer operation, a constant
 *          stored value and a direct callee are immediates
 *
 * Minimum: N >= 3 with C <= N - 2, M >= 2 with D <= M - 2.
 */
#include "vir-internal.h"
#include "vir/register-allocation.h"
#include "vir/synthetic-target.h"

static bool vir_synth_value_is_integer_constant(const vir_function *f, uint32_t v) { return v && v < f->constant_count && f->insns[v].op == VIR_OP_ICONST; }

/* the register of each value word of a signature's argument list (call order: SRET pointer,
 * then the groups), VIR_REGISTER_NONE for the stack; *nw words */
static uint32_t vir_synth_arguments_assign(const vir_synthetic_target *t, const vir_function *f, uint32_t sig, uint8_t *reg, uint32_t max,
		       uint32_t *grp0)
{
	const uint32_t *w = &f->extra[sig];
	uint32_t ng = VIR_SIGNATURE_GROUP_COUNT(w[0]), nw = 0, ri = 0, fi = 0;

	if (VIR_GROUP_KIND(w[1]) == VIR_GROUP_SRET) {
		if (nw < max)
			reg[nw] = ri < t->allocatable_integer_count ? VIR_REGISTER_MAKE(0, ri) : VIR_REGISTER_NONE;
		ri += ri < t->allocatable_integer_count;
		nw++;
	}
	for (uint32_t g = 0; g < ng; g++) {
		uint32_t w0 = w[3 + 2 * g], k = VIR_GROUP_KIND(w0), np = VIR_GROUP_PIECE_COUNT(w0), ni = 0, nf = 0;

		if (grp0)
			grp0[g] = nw;
		if (k == VIR_GROUP_MEM) {
			if (nw < max)
				reg[nw] = VIR_REGISTER_NONE;
			nw++;
			continue;
		}
		if (k != VIR_GROUP_REG)
			continue;
		for (uint32_t p = 0; p < np; p++)
			VIR_GROUP_CLASS(w0, p) == VIR_CLASS_INT ? ni++ : nf++;
		bool fits = ri + ni <= t->allocatable_integer_count && fi + nf <= t->allocatable_float_count;

		for (uint32_t p = 0; p < np; p++) {
			uint8_t r = VIR_REGISTER_NONE;

			if (fits)
				r = VIR_GROUP_CLASS(w0, p) == VIR_CLASS_INT ? VIR_REGISTER_MAKE(0, ri++) : VIR_REGISTER_MAKE(1, fi++);
			if (nw < max)
				reg[nw] = r;
			nw++;
		}
	}
	return nw;
}

/* the register of result piece p */
static uint8_t vir_synth_return_register(const vir_function *f, uint32_t sig, uint32_t p)
{
	uint32_t w0 = f->extra[sig + 1], ni = 0, nf = 0;

	if (VIR_GROUP_KIND(w0) != VIR_GROUP_REG || p >= VIR_GROUP_PIECE_COUNT(w0))
		return VIR_REGISTER_NONE;
	for (uint32_t i = 0; i < p; i++)
		VIR_GROUP_CLASS(w0, i) == VIR_CLASS_INT ? ni++ : nf++;
	if (VIR_GROUP_CLASS(w0, p) == VIR_CLASS_INT)
		return ni < 2 ? VIR_REGISTER_MAKE(0, ni) : VIR_REGISTER_NONE;
	return nf < 2 ? VIR_REGISTER_MAKE(1, nf) : VIR_REGISTER_NONE;
}

static void vir_synth_fixed_registers_get(const vir_function *f, uint32_t v, vir_register_constraint *x, void *ctx)
{
	const vir_synthetic_target *t = ctx;
	const vir_insn *in = &f->insns[v];
	uint8_t reg[VIR_REGISTER_CONSTRAINT_MAX];

	switch (in->op) {
	case VIR_OP_PARAM: {
		uint32_t gi[256], n;

		if (in->a == VIR_GROUP_RESULT) {
			x->result = t->allocatable_integer_count ? VIR_REGISTER_MAKE(0, 0) : VIR_REGISTER_NONE;
			return;
		}
		n = vir_synth_arguments_assign(t, f, f->signature, reg, VIR_REGISTER_CONSTRAINT_MAX, VIR_SIGNATURE_GROUP_COUNT(f->extra[f->signature]) <= 256 ? gi : NULL);
		if (VIR_SIGNATURE_GROUP_COUNT(f->extra[f->signature]) <= 256 && in->a < 256 && VIR_GROUP_KIND(f->extra[f->signature + 3 + 2 * in->a]) == VIR_GROUP_REG &&
		    gi[in->a] + in->b < n && gi[in->a] + in->b < VIR_REGISTER_CONSTRAINT_MAX)
			x->result = reg[gi[in->a] + in->b];
		return;
	}
	case VIR_OP_CALL: {
		uint32_t n = vir_synth_arguments_assign(t, f, in->b, reg, VIR_REGISTER_CONSTRAINT_MAX - 1, NULL);

		x->operand_count = (uint8_t)(n + 1 < VIR_REGISTER_CONSTRAINT_MAX ? n + 1 : VIR_REGISTER_CONSTRAINT_MAX);
		for (uint32_t i = 1; i < x->operand_count; i++) {
			x->operands[i] = reg[i - 1];
			if (reg[i - 1] == VIR_REGISTER_NONE)
				x->mem |= 1u << i;	/* on the stack: stored from wherever it is */
		}
		x->flags |= VIR_CONSTRAINT_FLAG_REST_MEMORY;
		if (f->insns[in->a].op == VIR_OP_SYMADDR)
			x->mem |= 1;
		x->clobbered[0] = t->registers.alloc[0] & ~t->registers.callee[0];
		x->clobbered[1] = t->registers.alloc[1] & ~t->registers.callee[1];
		if (in->type != VIR_TYPE_VOID && in->type != VIR_TYPE_MULTI)
			x->result = vir_synth_return_register(f, in->b, 0);
		return;
	}
	case VIR_OP_PROJ:
		x->result = vir_synth_return_register(f, f->insns[in->a].b, in->b);
		return;
	case VIR_OP_RET: {
		uint32_t n = f->extra[in->c];

		x->operand_count = (uint8_t)(n < VIR_REGISTER_CONSTRAINT_MAX ? n : VIR_REGISTER_CONSTRAINT_MAX);
		for (uint32_t i = 0; i < x->operand_count; i++)
			x->operands[i] = vir_synth_return_register(f, f->signature, i);
		return;
	}
	case VIR_OP_STORE:
		if (vir_opcode_is_constant(f->insns[in->b].op) && f->insns[in->b].op != VIR_OP_FCONST)
			x->mem |= 2;
		return;
	case VIR_OP_SDIV: case VIR_OP_UDIV: case VIR_OP_SREM: case VIR_OP_UREM:
		if (!t->is_x86)
			return;
		x->operand_count = 2;
		x->operands[0] = VIR_REGISTER_MAKE(0, 0);
		x->result = VIR_REGISTER_MAKE(0, in->op == VIR_OP_SDIV || in->op == VIR_OP_UDIV ? 0 : 1);
		x->edge_clobbered[0] = 2;
		x->clobbered[0] = 3;
		return;
	case VIR_OP_SHL: case VIR_OP_LSHR: case VIR_OP_ASHR:
		if (t->is_x86) {
			x->flags |= VIR_CONSTRAINT_FLAG_TIED;
			if (!vir_synth_value_is_integer_constant(f, in->b)) {
				x->operand_count = 2;
				x->operands[1] = VIR_REGISTER_MAKE(0, 2);
			}
		}
		if (vir_synth_value_is_integer_constant(f, in->b))
			x->mem |= 2;
		return;
	case VIR_OP_ADD: case VIR_OP_MUL: case VIR_OP_AND: case VIR_OP_OR: case VIR_OP_XOR:
		if (t->is_x86)
			x->flags |= VIR_CONSTRAINT_FLAG_TIED | VIR_CONSTRAINT_FLAG_COMMUTATIVE;
		if (vir_synth_value_is_integer_constant(f, in->b))
			x->mem |= 2;
		return;
	case VIR_OP_SUB:
		if (t->is_x86)
			x->flags |= VIR_CONSTRAINT_FLAG_TIED;
		if (vir_synth_value_is_integer_constant(f, in->b))
			x->mem |= 2;
		return;
	case VIR_OP_SWITCH:
		if (t->is_x86) {
			x->clobbered[0] = 1;       /* written after reading the selector */
			x->edge_clobbered[0] = 1u << 1;
		}
		return;
	case VIR_OP_ICMP:
		if (vir_synth_value_is_integer_constant(f, in->b))
			x->mem |= 2;
		return;
	case VIR_OP_NEG: case VIR_OP_NOT:
		if (t->is_x86)
			x->flags |= VIR_CONSTRAINT_FLAG_TIED;
		return;
	case VIR_OP_FADD: case VIR_OP_FMUL:
		if (t->is_x86)
			x->flags |= VIR_CONSTRAINT_FLAG_TIED | VIR_CONSTRAINT_FLAG_COMMUTATIVE;
		return;
	case VIR_OP_FSUB: case VIR_OP_FDIV:
		if (t->is_x86)
			x->flags |= VIR_CONSTRAINT_FLAG_TIED;
		return;
	default:
		return;
	}
}

static uint8_t vir_synth_operand_fixes[256];

bool vir_synth_init(vir_synthetic_target *t, uint32_t ni, uint32_t nf, uint32_t ci, uint32_t cf, bool x86, char *err, size_t en)
{
	static const uint8_t ops[] = { VIR_OP_PARAM, VIR_OP_CALL, VIR_OP_PROJ, VIR_OP_RET, VIR_OP_STORE, VIR_OP_SDIV, VIR_OP_UDIV, VIR_OP_SREM, VIR_OP_UREM,
				       VIR_OP_SHL, VIR_OP_LSHR, VIR_OP_ASHR, VIR_OP_ADD, VIR_OP_MUL, VIR_OP_AND, VIR_OP_OR, VIR_OP_XOR, VIR_OP_SUB, VIR_OP_ICMP,
				       VIR_OP_NEG, VIR_OP_NOT, VIR_OP_FADD, VIR_OP_FMUL, VIR_OP_FSUB, VIR_OP_FDIV, VIR_OP_SWITCH };

	if (ni < 3 || ni > 64 || nf < 2 || nf > 64 || ci + 2 > ni || cf + 2 > nf) {
		snprintf(err, en, "registers: need 3 <= N <= 64 with C <= N-2, 2 <= M <= 64 with D <= M-2 (N %u M %u C %u D %u)",
			 ni, nf, ci, cf);
		return false;
	}
	memset(t, 0, sizeof(*t));
	for (size_t i = 0; i < sizeof(ops); i++)
		vir_synth_operand_fixes[ops[i]] = 1;
	t->is_x86 = x86;
	t->registers.alloc[0] = ni == 64 ? ~0ull : (1ull << ni) - 1;
	t->registers.alloc[1] = nf == 64 ? ~0ull : (1ull << nf) - 1;
	t->registers.callee[0] = t->registers.alloc[0] & ~((1ull << (ni - ci)) - 1);
	t->registers.callee[1] = t->registers.alloc[1] & ~((1ull << (nf - cf)) - 1);
	t->allocatable_integer_count = ni - ci - 1 < 6 ? ni - ci - 1 : 6;
	t->allocatable_float_count = nf - cf < 8 ? nf - cf : 8;
	t->registers.fixed = vir_synth_fixed_registers_get;
	t->registers.context = t;
	t->registers.operand_fixes = vir_synth_operand_fixes;
	return true;
}
