/* A bounded one-destination switch membership test. The shared planner proves
 * the case-label topology; the existing switch still emits every body segment.
 */
#include "wimpl.h"

VPRIV bool wswitch_bitset(ecx *e, uint32_t body, uint32_t sw0, uint32_t ncase,
			 uint32_t nlabels, uint32_t t, int k, bool sg,
			 int dflt, const int *sb)
{
	em_switch_bitset_recipe recipe;

	if (!ncase || (k != WT_I32 && k != WT_I64) ||
	    !em_switch_bitset_plan(e->c, body, nlabels, k == WT_I32 ? 32 : 64, sg, &recipe))
		return false;
	/* All explicit labels form one stacked run. The last collected case
	 * therefore has their common target segment; default is collected apart.
	 */
	int target = sb[e->w->sw[sw0 + ncase - 1].seg];
	int masktype = recipe.bits == 32 ? WT_I32 : WT_I64;

	wget(e, t);
	if (recipe.lo) {
		wconst(e, k, (int64_t)recipe.lo);
		wop(e, k, "sub");
		wtee(e, t);
	}
	wconst(e, k, recipe.span);
	wop(e, k, "gt_u");
	wbr(e, "br_if", dflt, true);

	/* Guard before shifting: wasm masks shift counts modulo word width. */
	wconst(e, masktype, (int64_t)recipe.mask);
	wget(e, t);
	if (k != masktype)
		wi(e, masktype == WT_I64 ? "i64.extend_i32_u" : "i32.wrap_i64");
	wop(e, masktype, "shr_u");
	wconst(e, masktype, 1);
	wop(e, masktype, "and");
	if (masktype == WT_I64)
		wi(e, "i32.wrap_i64");
	wbr(e, "br_if", target, true);
	wbr(e, "br", dflt, false);
	return true;
}
