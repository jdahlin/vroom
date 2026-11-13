/*
 * sdivconst.c: signed division or remainder by a constant as a multiply-high, shifts and a sign
 * correction, for the walker: the code behind the recipe of opt/sdivconst.c.
 *
 * wk_sdivconst_ok says whether the target has the registers the sequence needs (the saved input and
 * the magic constant are the peak, LO(nfree) >= extra); wk_sdivconst then builds the sequence through
 * the lowering interface LO() (smulhi for 64 bits, a widened multiply for 32), so it is the same for
 * every CPU and for VIR.  The caller checks _ok first and keeps operand evaluation order.
 */
#include "walk.h"

/* The saved input and materialized magic are the peak two extra registers.
 * The final sign-bit temporary reuses the magic's released allocation. */
VPRIV bool wk_sdivconst_ok(ecx *e, val a, const em_sdivconst_recipe *r)
{
	int extra = (a.k != V_REG) + 1 + (r->add || r->remainder);
	return LO(nfree)(e) >= extra;
}

VPRIV val wk_sdivconst(ecx *e, ast_qualified_type ty, val a, const em_sdivconst_recipe *r)
{
	int size = r->bits / 8, input = wk_reg(e, &a, ty), saved = -1;
	val q;

	if (r->bits == 32)
		LO(ext)(e, input, 4, true);
	if (r->add || r->remainder) {
		saved = LO(treg)(e);
		LO(mov)(e, saved, input);
	}
	if (r->bits == 64)
		q = LO(smulhi)(e, a, vconst(r->magic));
	else {
		q = LO(bin)(e, LEX_TOKEN_STAR, 8, false, a, vconst(r->magic));
		q = LO(bin)(e, LEX_TOKEN_GREATER_GREATER, 8, true, q, vconst(32));
	}
	if (r->add) {
		val n = { .k=V_REG, .r=(uint8_t)saved };

		q = LO(bin)(e, LEX_TOKEN_PLUS, size, false, q, n);
		if (r->remainder)
			LO(tkeep)(e, saved);
	}
	if (r->shift)
		q = LO(bin)(e, LEX_TOKEN_GREATER_GREATER, size, true, q, vconst(r->shift));
	{
		int sign = LO(treg)(e);
		val s = { .k=V_REG, .r=(uint8_t)sign };

		LO(mov)(e, sign, q.r);
		s = LO(bin)(e, LEX_TOKEN_GREATER_GREATER, size, false, s, vconst(r->bits - 1));
		q = LO(bin)(e, LEX_TOKEN_PLUS, size, false, q, s);
	}
	if (r->remainder) {
		q = LO(bin)(e, LEX_TOKEN_STAR, size, false, q, vconst(r->divisor));
		return LO(bin)(e, LEX_TOKEN_MINUS, size, false, (val){ .k=V_REG, .r=(uint8_t)saved }, q);
	}
	return r->negative ? LO(un)(e, LEX_TOKEN_MINUS, size, q) : q;
}
