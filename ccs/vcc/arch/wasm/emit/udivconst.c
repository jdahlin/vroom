#include "wimpl.h"

/* Expand only the compact quotient recipes. Add-corrected reciprocals and
 * remainders keep wasm's div/rem instructions for the execution engine. */
VPRIV bool wudivconst_ok(const em_udivconst_recipe *r)
{
	return !r->remainder && (r->compare || !r->add);
}

/* stack: [unsigned i32 lhs]. The caller checked wudivconst_ok(). */
VPRIV void wudivconst(ecx *e, const em_udivconst_recipe *r)
{
	if (r->compare) {
		wconst(e, WT_I32, (int64_t)r->divisor);
		wi(e, "i32.ge_u");
		return;
	}
	wi(e, "i64.extend_i32_u");
	wconst(e, WT_I64, (int64_t)r->magic);
	wi(e, "i64.mul");
	wconst(e, WT_I64, 32 + r->shift);
	wi(e, "i64.shr_u");
	wi(e, "i32.wrap_i64");
}
