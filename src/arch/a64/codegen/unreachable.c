/* arm64 flow classification and debug adapter for shared unreachable-record cleanup. */
#include "../../../codegen/codegen-internal.h"

typedef struct a64_unreachable_debug {
	codegen_context *e;
	uint32_t cursor;
} a64_unreachable_debug;

static void a64_unreachable_remap(void *ctx, uint32_t old, uint32_t next)
{
	a64_unreachable_debug *d = ctx;

	codegen_debug_rows_remap(d->e, &d->cursor, old, next);
}

VPRIV void a64_unreachable_remove(codegen_context *e)
{
	/* Every understood ordinary record is explicit. New opcodes default
	 * to entry barriers, as do raw text and encoded instructions. BRK is
	 * resumable (__debugtrap and __arm_trap), so it does not end a region.
	 */
#define A64_NEXT(op) [op] = RECORD_FLOW_NEXT
	static const uint8_t flow[A64_OP_STF + 1] = {
		[A64_OP_B] = RECORD_FLOW_STOP, [A64_OP_RET] = RECORD_FLOW_STOP, [A64_OP_TAIL] = RECORD_FLOW_STOP,
		A64_NEXT(A64_OP_ADD), A64_NEXT(A64_OP_SUB), A64_NEXT(A64_OP_ADDS), A64_NEXT(A64_OP_SUBS),
		A64_NEXT(A64_OP_AND), A64_NEXT(A64_OP_ORR), A64_NEXT(A64_OP_EOR), A64_NEXT(A64_OP_ANDS),
		A64_NEXT(A64_OP_BIC), A64_NEXT(A64_OP_ORN), A64_NEXT(A64_OP_ADC), A64_NEXT(A64_OP_SBC),
		A64_NEXT(A64_OP_ADCS), A64_NEXT(A64_OP_SBCS),
		A64_NEXT(A64_OP_ADDI), A64_NEXT(A64_OP_SUBI), A64_NEXT(A64_OP_ADDSI), A64_NEXT(A64_OP_SUBSI),
		A64_NEXT(A64_OP_ANDI), A64_NEXT(A64_OP_ORRI), A64_NEXT(A64_OP_EORI), A64_NEXT(A64_OP_ANDSI),
		A64_NEXT(A64_OP_MOVI), A64_NEXT(A64_OP_MOV), A64_NEXT(A64_OP_MADD), A64_NEXT(A64_OP_MSUB),
		A64_NEXT(A64_OP_SMADDL), A64_NEXT(A64_OP_UMADDL),
		A64_NEXT(A64_OP_SDIV), A64_NEXT(A64_OP_UDIV), A64_NEXT(A64_OP_SMULH), A64_NEXT(A64_OP_UMULH),
		A64_NEXT(A64_OP_LSLV), A64_NEXT(A64_OP_LSRV), A64_NEXT(A64_OP_ASRV),
		A64_NEXT(A64_OP_SBFM), A64_NEXT(A64_OP_UBFM), A64_NEXT(A64_OP_BFM),
		A64_NEXT(A64_OP_CSEL), A64_NEXT(A64_OP_CSINC), A64_NEXT(A64_OP_CSINV), A64_NEXT(A64_OP_CSNEG),
		A64_NEXT(A64_OP_CLZ), A64_NEXT(A64_OP_CLS), A64_NEXT(A64_OP_RBIT), A64_NEXT(A64_OP_REV),
		A64_NEXT(A64_OP_REV16), A64_NEXT(A64_OP_REV32), A64_NEXT(A64_OP_LD), A64_NEXT(A64_OP_ST), A64_NEXT(A64_OP_LDX), A64_NEXT(A64_OP_STX),
		A64_NEXT(A64_OP_LDP), A64_NEXT(A64_OP_STP), A64_NEXT(A64_OP_ADRP), A64_NEXT(A64_OP_ADDLO),
		A64_NEXT(A64_OP_LDLO), A64_NEXT(A64_OP_STLO), A64_NEXT(A64_OP_LDGOT), A64_NEXT(A64_OP_ADR),
		A64_NEXT(A64_OP_BC), A64_NEXT(A64_OP_CBZ), A64_NEXT(A64_OP_CBNZ), A64_NEXT(A64_OP_TBZ), A64_NEXT(A64_OP_TBNZ), A64_NEXT(A64_OP_BL),
		A64_NEXT(A64_OP_BLR), A64_NEXT(A64_OP_BRK), A64_NEXT(A64_OP_PRFM), A64_NEXT(A64_OP_MRS),
		A64_NEXT(I_MOVSP), A64_NEXT(A64_OP_NOP), A64_NEXT(A64_OP_TLV), A64_NEXT(A64_OP_LDF), A64_NEXT(A64_OP_STF),
	};
#undef A64_NEXT
	a64_unreachable_debug d = { e, 0 };

	e->record_count = record_unreachable_remove(e->records, e->record_count, sizeof(a64_record), offsetof(a64_record, op), flow, sizeof(flow),
				    e->debug_row_count ? a64_unreachable_remap : NULL, &d);
}
