/* High products, using implicit RDX:RAX unsigned/signed multiplication. */
#include "x64impl.h"

static val x64_mulhi(ecx *e, val a, val b, int op)
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
	x64_op3(e, op, 8, XNOREG, src, XNOREG);
	x64_mov(e, ra, RDX);
	x64_restore(e, saved);
	/* The result retains a's register ownership, even when both inputs alias. */
	if (rb != ra)
		x64_tfree(e, rb);
	return (val){ .k = V_REG, .r = (uint8_t)ra };
}

VPRIV val x64_umulhi(ecx *e, val a, val b)
{
	return x64_mulhi(e, a, b, X_MULW);
}

VPRIV val x64_smulhi(ecx *e, val a, val b)
{
	return x64_mulhi(e, a, b, X_SMULW);
}
