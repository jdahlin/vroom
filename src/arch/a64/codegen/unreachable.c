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

VPRIV void a64_unreachable(codegen_context *e)
{
	/* Every understood ordinary record is explicit. New opcodes default
	 * to entry barriers, as do raw text and encoded instructions. BRK is
	 * resumable (__debugtrap and __arm_trap), so it does not end a region.
	 */
#define A64_NEXT(op) [op] = RECORD_FLOW_NEXT
	static const uint8_t flow[I_STF + 1] = {
		[I_B] = RECORD_FLOW_STOP, [I_RET] = RECORD_FLOW_STOP, [I_TAIL] = RECORD_FLOW_STOP,
		A64_NEXT(I_ADD), A64_NEXT(I_SUB), A64_NEXT(I_ADDS), A64_NEXT(I_SUBS),
		A64_NEXT(I_AND), A64_NEXT(I_ORR), A64_NEXT(I_EOR), A64_NEXT(I_ANDS),
		A64_NEXT(I_BIC), A64_NEXT(I_ORN), A64_NEXT(I_ADC), A64_NEXT(I_SBC),
		A64_NEXT(I_ADCS), A64_NEXT(I_SBCS),
		A64_NEXT(I_ADDI), A64_NEXT(I_SUBI), A64_NEXT(I_ADDSI), A64_NEXT(I_SUBSI),
		A64_NEXT(I_ANDI), A64_NEXT(I_ORRI), A64_NEXT(I_EORI), A64_NEXT(I_ANDSI),
		A64_NEXT(I_MOVI), A64_NEXT(I_MOV), A64_NEXT(I_MADD), A64_NEXT(I_MSUB),
		A64_NEXT(I_SMADDL), A64_NEXT(I_UMADDL),
		A64_NEXT(I_SDIV), A64_NEXT(I_UDIV), A64_NEXT(I_SMULH), A64_NEXT(I_UMULH),
		A64_NEXT(I_LSLV), A64_NEXT(I_LSRV), A64_NEXT(I_ASRV),
		A64_NEXT(I_SBFM), A64_NEXT(I_UBFM), A64_NEXT(I_BFM),
		A64_NEXT(I_CSEL), A64_NEXT(I_CSINC), A64_NEXT(I_CSINV), A64_NEXT(I_CSNEG),
		A64_NEXT(I_CLZ), A64_NEXT(I_CLS), A64_NEXT(I_RBIT), A64_NEXT(I_REV),
		A64_NEXT(I_REV16), A64_NEXT(I_REV32), A64_NEXT(I_LD), A64_NEXT(I_ST), A64_NEXT(I_LDX), A64_NEXT(I_STX),
		A64_NEXT(I_LDP), A64_NEXT(I_STP), A64_NEXT(I_ADRP), A64_NEXT(I_ADDLO),
		A64_NEXT(I_LDLO), A64_NEXT(I_STLO), A64_NEXT(I_LDGOT), A64_NEXT(I_ADR),
		A64_NEXT(I_BC), A64_NEXT(I_CBZ), A64_NEXT(I_CBNZ), A64_NEXT(I_TBZ), A64_NEXT(I_TBNZ), A64_NEXT(I_BL),
		A64_NEXT(I_BLR), A64_NEXT(I_BRK), A64_NEXT(I_PRFM), A64_NEXT(I_MRS),
		A64_NEXT(I_MOVSP), A64_NEXT(I_NOP), A64_NEXT(I_TLV), A64_NEXT(I_LDF), A64_NEXT(I_STF),
	};
#undef A64_NEXT
	a64_unreachable_debug d = { e, 0 };

	e->nin = record_unreachable_remove(e->in, e->nin, sizeof(ins), offsetof(ins, op), flow, sizeof(flow),
				    e->ndl ? a64_unreachable_remap : NULL, &d);
}
