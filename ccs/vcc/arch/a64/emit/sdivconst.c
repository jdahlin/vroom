/* Signed constant division: explicit signed high product and truncation to zero. */
#include "../../../emit/emitimpl.h"

VPRIV bool a64_sdivconst_ok(ecx *e, val a, const em_sdivconst_recipe *r)
{
	unsigned available = NTEMP - (unsigned)__builtin_popcount((e->live | e->resv) & ((1u << NTEMP) - 1));
	unsigned needed = (a.k != V_REG) + 1 + (r->add || r->remainder);
	return available >= needed;
}

VPRIV val a64_sdivconst(ecx *e, val a, const em_sdivconst_recipe *r)
{
	int x = r->bits == 64, n = toreg(e, &a);
	int q = r->add || r->remainder ? treg(e) : n;
	int m = treg(e);

	if (!x)
		extend(e, n, n, 4, true);
	movi(e, 1, m, r->magic);
	if (x)
		op3(e, I_SMULH, 1, q, n, m);
	else {
		/* Both inputs are sign-extended32; the full product fits in64. */
		op4(e, I_MADD, 1, q, n, m, ZR);
		shift_imm(e, I_ASRV, 1, q, q, 32);
	}
	if (r->add)
		op3(e, I_ADD, x, q, q, n);
	if (r->shift)
		shift_imm(e, I_ASRV, x, q, q, r->shift);
	shift_imm(e, I_LSRV, x, m, q, r->bits - 1);
	op3(e, I_ADD, x, q, q, m);
	if (r->remainder) {
		movi(e, x, m, (int64_t)r->divisor);
		op4(e, I_MSUB, x, n, q, m, n);
	} else {
		if (r->negative)
			op3(e, I_SUB, x, q, ZR, q);
		if (q != n)
			mov(e, x, n, q);
	}
	if (q != n)
		tfree(e, q);
	tfree(e, m);
	return a;
}
