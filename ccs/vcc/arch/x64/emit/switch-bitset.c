/* One guarded membership mask, using existing scalar lowering operations. */
#include "x64impl.h"

VPRIV void x64_switch_bitset(ecx *e, int source, int size,
			    const em_switch_bitset_recipe *recipe,
			    uint32_t target, uint32_t dflt)
{
	int index = x64_treg(e);
	val offset = { .k = V_REG, .r = (uint8_t)index };
	int width = recipe->bits / 8;

	x64_op3(e, X_MOV, size, index, source, XNOREG);
	if (recipe->lo)
		offset = x64_bin(e, T_MINUS, size, false, offset, vconst(recipe->lo));
	/* x86 masks variable shift counts. Reject every index outside the mask
	 * before the shift, including negative and wrapped controlling values. */
	x64_cmpbr(e, T_GREATER, size, false, offset, vconst(recipe->span), dflt);
	x64_tkeep(e, offset.r);
	val member = x64_bin(e, T_GREATERGREATER, width, false,
			     vconst((i128)recipe->mask), offset);
	member = x64_bin(e, T_AMP, width, false, member, vconst(1));
	x64_brz(e, member, width, true, target);
	x64_jump(e, dflt);
}
