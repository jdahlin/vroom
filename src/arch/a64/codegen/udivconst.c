/* Unsigned reciprocal recipes use baseline scalar A64 instructions only.
 * At most 12 instructions for unsigned32, 14 for unsigned64, including
 * normalization/constants and remainder (64-bit constants need up to four).
 * Unsigned32 recipes without correction merge extraction and final shift. */
#include "../../../codegen/emitimpl.h"

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
	int x = r->bits == 64;
	int n = toreg(e, &a);
	int q = r->add || r->remainder ? treg(e) : n;
	int m = treg(e);

	/* A lazy narrowing cast may leave high garbage. MADD below is 64-bit. */
	if (!x)
		extend(e, n, n, 4, false);
	if (r->compare) {
		movi(e, x, m, (int64_t)r->divisor);
		op3(e, I_SUBS, x, ZR, n, m);
		cset(e, q, C_HS);
	} else {
		movi(e, x, m, (int64_t)r->magic);
		if (x)
			op3(e, I_UMULH, 1, q, n, m);
		else
			op4(e, I_MADD, 1, q, n, m, ZR);
		if (!r->add) {
			if (!x || r->shift)
				shift_imm(e, I_LSRV, 1, r->remainder ? q : n, q,
				          (x ? 0 : 32) + r->shift);
		} else {
			if (!x)
				shift_imm(e, I_LSRV, 1, q, q, 32);
			op3(e, I_SUB, x, m, n, q);
			shift_imm(e, I_LSRV, x, m, m, 1);
			op3(e, I_ADD, x, q, m, q);
			if (r->shift)
				shift_imm(e, I_LSRV, x, r->remainder ? q : n, q, r->shift);
			else if (!r->remainder)
				mov(e, x, n, q);
		}
	}
	if (r->remainder) {
		if (!r->compare)
			movi(e, x, m, (int64_t)r->divisor);
		op4(e, I_MSUB, x, n, q, m, n);
	}
	if (q != n)
		tfree(e, q);
	tfree(e, m);
	return a;
}
