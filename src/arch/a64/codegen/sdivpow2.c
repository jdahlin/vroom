#include "../../../codegen/codegen-internal.h"

/* The shared recipe specifies rounding; this adapter owns registers. */
VPRIV codegen_value a64_sdivpow2(codegen_context *e, codegen_value a, const em_sdivpow2_recipe *r)
{
	int x = r->bits == 64, ra = toreg(e, &a), bias;

	if (!r->shift) {
		if (r->remainder)
			a64_record_emit_move_immediate(e, x, ra, 0);
		else if (r->negative)
			a64_record_emit_three_registers(e, A64_OP_SUB, x, ra, ZR, ra);
		return a;
	}
	bias = treg(e);
	shift_imm(e, A64_OP_ASRV, x, bias, ra, r->bits - 1);
	shift_imm(e, A64_OP_LSRV, x, bias, bias, r->bits - r->shift);
	a64_record_emit_three_registers(e, A64_OP_ADD, x, ra, ra, bias);
	if (r->remainder) {
		a64_record_emit_bitfield(e, A64_OP_UBFM, x, ra, ra, 0, r->shift - 1);
		a64_record_emit_three_registers(e, A64_OP_SUB, x, ra, ra, bias);
	} else {
		shift_imm(e, A64_OP_ASRV, x, ra, ra, r->shift);
		if (r->negative)
			a64_record_emit_three_registers(e, A64_OP_SUB, x, ra, ZR, ra);
	}
	tfree(e, bias);
	return a;
}
