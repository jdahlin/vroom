/* Dispatch one stacked case run with a bounded membership mask. The selector
 * remains borrowed; only the index and shifted mask own temporaries.
 */
#include "../../../codegen/emitimpl.h"

VPRIV bool a64_switch_bitset(ecx *e, uint32_t body, uint32_t base, int r, int cx,
			     bool sg, uint32_t dflt)
{
	em_switch_bitset_recipe recipe;

	if (e->E->o->opt < 1 || e->E->noopt ||
	    !em_switch_bitset_plan(e->c, body, e->ncases - base, cx ? 64 : 32, sg, &recipe))
		return false;
	int index = treg(e), mask = treg(e);
	int mx = recipe.bits == 64;

	/* A 32-bit subtraction also clears the index's upper bits when the
	 * membership mask needs a 64-bit variable shift. Guard first: A64 masks
	 * variable shift counts, which would otherwise alias out-of-range cases.
	 */
	if (recipe.lo)
		opi(e, I_SUBI, cx, index, r, (int64_t)recipe.lo);
	else
		mov(e, cx, index, r);
	opi(e, I_SUBSI, cx, ZR, index, recipe.span);
	bcond(e, C_HI, dflt);
	movi(e, mx, mask, (int64_t)recipe.mask);
	op3(e, I_LSRV, mx, mask, mask, index);
	opi(e, I_ANDI, mx, mask, mask, 1);
	cbz(e, true, mx, mask, e->caselbl[recipe.node]);
	tfree(e, index);
	tfree(e, mask);
	jump(e, dflt);
	return true;
}
