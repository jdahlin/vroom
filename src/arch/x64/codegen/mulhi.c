/* High products, using implicit RDX:RAX unsigned/signed multiplication. */
#include "x64-internal.h"

static codegen_value x64_mulhi(codegen_context *e, codegen_value a, codegen_value b, int op)
{
	int ra = x64_toreg(e, &a, 8), rb = x64_toreg(e, &b, 8), src = rb;
	static const uint8_t fixed[2] = { RAX, RDX };
	/* Both operands are consumed; only unrelated live fixed registers survive. */
	uint32_t saved = x64_save(e, fixed, 2, ra, rb);

	/* Preserve b before moving a into RAX. R11 is reserved scratch, so this
	 * also handles a and b sharing either implicit operand register. */
	if (rb == RAX || rb == RDX) {
		x64_mov(e, R11, rb);
		src = R11;
	}
	x64_mov(e, RAX, ra);
	x64_record_emit_three_registers(e, op, 8, X64_REGISTER_NONE, src, X64_REGISTER_NONE);
	x64_mov(e, ra, RDX);
	x64_restore(e, saved);
	/* The result retains a's register ownership, even when both inputs alias. */
	if (rb != ra)
		x64_tfree(e, rb);
	return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)ra };
}

VPRIV codegen_value x64_umulhi(codegen_context *e, codegen_value a, codegen_value b)
{
	return x64_mulhi(e, a, b, X64_OP_MULW);
}

VPRIV codegen_value x64_smulhi(codegen_context *e, codegen_value a, codegen_value b)
{
	return x64_mulhi(e, a, b, X64_OP_SMULW);
}
