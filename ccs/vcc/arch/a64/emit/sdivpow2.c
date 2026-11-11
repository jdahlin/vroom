#include "../../../emit/emitimpl.h"

/* The shared recipe specifies rounding; this adapter owns registers. */
VPRIV val a64_sdivpow2(ecx *e, val a, const em_sdivpow2_recipe *r)
{
	int x = r->bits == 64, ra = toreg(e, &a), bias;

	if (!r->shift) {
		if (r->remainder)
			movi(e, x, ra, 0);
		else if (r->negative)
			op3(e, I_SUB, x, ra, ZR, ra);
		return a;
	}
	bias = treg(e);
	shift_imm(e, I_ASRV, x, bias, ra, r->bits - 1);
	shift_imm(e, I_LSRV, x, bias, bias, r->bits - r->shift);
	op3(e, I_ADD, x, ra, ra, bias);
	if (r->remainder) {
		opbf(e, I_UBFM, x, ra, ra, 0, r->shift - 1);
		op3(e, I_SUB, x, ra, ra, bias);
	} else {
		shift_imm(e, I_ASRV, x, ra, ra, r->shift);
		if (r->negative)
			op3(e, I_SUB, x, ra, ZR, ra);
	}
	tfree(e, bias);
	return a;
}

