#include "wimpl.h"

/* Keep wasm's compact constant division for its execution engine, as LLVM's
 * wasm lowering does. Only recipes cheaper than div/rem are expanded here.
 */
VPRIV bool wsdivpow2_ok(const em_sdivpow2_recipe *r)
{
	return !r->shift || (!r->remainder && r->shift == r->bits - 1);
}

/* stack: [lhs]. Callers have already checked wsdivpow2_ok(). */
VPRIV void wsdivpow2(ecx *e, ast_qualified_type ty, const em_sdivpow2_recipe *r)
{
	int k = wty(e->c, ty);

	if (r->shift) {
		/* n / MIN is one iff n == MIN. The comparison itself returns i32. */
		wconst(e, k, (int64_t)(-((i128)1 << (r->bits - 1))));
		wop(e, k, "eq");
		if (k == WT_I64)
			wi(e, "i64.extend_i32_u");
	} else if (r->remainder) {
		wi(e, "drop");
		wconst(e, k, 0);
	} else if (r->negative) {
		wconst(e, k, -1);
		wop(e, k, "mul");
	}
}
