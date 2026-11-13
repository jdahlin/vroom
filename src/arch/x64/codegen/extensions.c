/* x86 GPR writes whose result is already zero-extended from 32 bits.
 * Inspect only the immediately preceding record: no scan, facts or allocation.
 */
#include "x64impl.h"

VPRIV bool x64_last_u32(const ecx *e, int r)
{
	const xst *x = e->x64;
	if (r < 0 || r >= XMM0 || !x->nin)
		return false;
	const rec *i = &x->in[x->nin - 1];
	if (i->r[0] != r)
		return false;
	if (i->op == X_MOVI)
		return (uint64_t)i->imm <= UINT32_MAX;
	if (i->op == X_LD || i->op == X_LDX || i->op == X_EXT)
		return !i->cond && (i->sz == 1 || i->sz == 2 || i->sz == 4);
	if (i->sz != 4)
		return false;
	switch (i->op) {
	case X_MOV:
	case X_LEA: case X_LEASCALE:
	case X_ADD: case X_SUB: case X_AND: case X_OR: case X_XOR:
	case X_IMUL: case X_ADC: case X_SBB:
	case X_ADDI: case X_SUBI: case X_ANDI: case X_ORI: case X_XORI: case X_IMULI:
	case X_NEG: case X_NOT: case X_BSWAP:
	case X_SETCC: case X_FSETCC:
		return true;
	default:
		/* Shifts by zero and BSF/BSR with zero inputs need special care.
		 * Unknown, partial, signed and non-writing records prove nothing. */
		return false;
	}
}
