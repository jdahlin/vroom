/* One guarded membership mask, using existing scalar lowering operations. */
#include "x64impl.h"

VPRIV void x64_switch_bitset(codegen_context *e, int source, int size,
			    const em_switch_bitset_recipe *recipe,
			    uint32_t target, uint32_t dflt)
{
	int index = x64_treg(e);
	codegen_value offset = { .k = CODEGEN_VALUE_REGISTER, .r = (uint8_t)index };
	int width = recipe->bits / 8;

	x64_op3(e, X_MOV, size, index, source, XNOREG);
	if (recipe->lo)
		offset = x64_bin(e, LEX_TOKEN_MINUS, size, false, offset, codegen_value_constant(recipe->lo));
	/* x86 masks variable shift counts. Reject every index outside the mask
	 * before the shift, including negative and wrapped controlling values. */
	x64_cmpbr(e, LEX_TOKEN_GREATER, size, false, offset, codegen_value_constant(recipe->span), dflt);
	x64_tkeep(e, offset.r);
	codegen_value member = x64_bin(e, LEX_TOKEN_GREATER_GREATER, width, false,
			     codegen_value_constant((i128)recipe->mask), offset);
	member = x64_bin(e, LEX_TOKEN_AMPERSAND, width, false, member, codegen_value_constant(1));
	x64_brz(e, member, width, true, target);
	x64_jump(e, dflt);
}
