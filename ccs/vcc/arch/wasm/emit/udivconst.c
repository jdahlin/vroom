#include "wimpl.h"

/* Expand only the compact quotient recipes. Add-corrected reciprocals and
 * remainders keep wasm's div/rem instructions for the execution engine. */
VPRIV bool wudivconst_ok(const em_udivconst_recipe *r)
{
	return !r->remainder && (r->compare || (r->bits == 32 && !r->add));
}

/* stack: [unsigned i32 lhs]. The caller checked wudivconst_ok(). */
VPRIV void wudivconst(ecx *e, const em_udivconst_recipe *r)
{
	if (r->compare) {
		int k = r->bits == 64 ? WT_I64 : WT_I32;
		wconst(e, k, (int64_t)r->divisor);
		wop(e, k, "ge_u");
		if (r->bits == 64)
			wi(e, "i64.extend_i32_u");
		return;
	}
	wi(e, "i64.extend_i32_u");
	wconst(e, WT_I64, (int64_t)r->magic);
	wi(e, "i64.mul");
	wconst(e, WT_I64, 32 + r->shift);
	wi(e, "i64.shr_u");
	wi(e, "i32.wrap_i64");
}
