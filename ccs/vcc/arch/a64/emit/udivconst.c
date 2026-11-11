/* Unsigned32 reciprocal recipes use baseline scalar A64 instructions only.
 * At most 12 instructions including normalization/constants and remainder;
 * recipes without correction merge high-half extraction with the final shift. */
#include "../../../emit/emitimpl.h"

/* One magic/divisor register plus a distinct quotient for correction/rem.
 * Include materializing a lazy address; never provoke an allocator retry. */
VPRIV bool a64_udivconst_ok(ecx *e, val a, const em_udivconst_recipe *r)
{
	unsigned available = NTEMP - (unsigned)__builtin_popcount((e->live | e->resv) & ((1u << NTEMP) - 1));
	unsigned needed = (a.k != V_REG) + 1 + (r->add || r->remainder);
	return available >= needed;
}

VPRIV val a64_udivconst(ecx *e, val a, const em_udivconst_recipe *r)
{
	int n = toreg(e, &a);
	int q = r->add || r->remainder ? treg(e) : n;
	int m = treg(e);

	/* A lazy narrowing cast may leave high garbage. MADD below is 64-bit. */
	extend(e, n, n, 4, false);
	if (r->compare) {
		movi(e, 0, m, r->divisor);
		op3(e, I_SUBS, 0, ZR, n, m);
		cset(e, q, C_HS);
	} else {
		movi(e, 0, m, r->magic);
		op4(e, I_MADD, 1, q, n, m, ZR);
		if (!r->add) {
			shift_imm(e, I_LSRV, 1, r->remainder ? q : n, q, 32 + r->shift);
		} else {
			shift_imm(e, I_LSRV, 1, q, q, 32);
			op3(e, I_SUB, 0, m, n, q);
			shift_imm(e, I_LSRV, 0, m, m, 1);
			op3(e, I_ADD, 0, q, m, q);
			if (r->shift)
				shift_imm(e, I_LSRV, 0, r->remainder ? q : n, q, r->shift);
			else if (!r->remainder)
				mov(e, 0, n, q);
		}
	}
	if (r->remainder) {
		if (!r->compare)
			movi(e, 0, m, r->divisor);
		op4(e, I_MSUB, 0, n, q, m, n);
	}
	if (q != n)
		tfree(e, q);
	tfree(e, m);
	return a;
}
