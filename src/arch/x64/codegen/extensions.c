/* x86 GPR writes whose result is already zero-extended from 32 bits.
 * Inspect only the immediately preceding record: no scan, facts or allocation.
 */
#include "x64-internal.h"

VPRIV bool x64_last_u32(const codegen_context *e, int r)
{
	const x64_lowering_context *x = e->x64_context;
	if (r < 0 || r >= XMM0 || !x->record_count)
		return false;
	const record *i = &x->records[x->record_count - 1];
	if (i->registers[0] != r)
		return false;
	if (i->opcode == X64_OP_MOVI)
		return (uint64_t)i->immediate <= UINT32_MAX;
	if (i->opcode == X64_OP_LD || i->opcode == X64_OP_LDX || i->opcode == X64_OP_EXT)
		return !i->condition && (i->size == 1 || i->size == 2 || i->size == 4);
	if (i->size != 4)
		return false;
	switch (i->opcode) {
	case X64_OP_MOV:
	case X64_OP_LEA: case X64_OP_LEASCALE:
	case X64_OP_ADD: case X64_OP_SUB: case X64_OP_AND: case X64_OP_OR: case X64_OP_XOR:
	case X64_OP_IMUL: case X64_OP_ADC: case X64_OP_SBB:
	case X64_OP_ADDI: case X64_OP_SUBI: case X64_OP_ANDI: case X64_OP_ORI: case X64_OP_XORI: case X64_OP_IMULI:
	case X64_OP_NEG: case X64_OP_NOT: case X64_OP_BSWAP:
	case X64_OP_SETCC: case X64_OP_FSETCC:
		return true;
	default:
		/* Shifts by zero and BSF/BSR with zero inputs need special care.
		 * Unknown, partial, signed and non-writing records prove nothing. */
		return false;
	}
}
