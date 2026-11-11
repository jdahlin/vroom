#include "walk.h"

/* During multiplication, a saved numerator and a materialized large magic
 * need two extra registers. Correction/rematerialized divisor never exceed
 * that peak; simple quotients need only the magic. A lazy address needs its
 * own input register before any of these temporaries are allocated. */
VPRIV bool wk_udivconst_ok(ecx *e, val a, const em_udivconst_recipe *r)
{
	int extra = (a.k != V_REG) + 1 + (r->add || r->remainder);
	return LO(nfree)(e) >= extra;
}

/* Widening a normalized unsigned32 input makes its complete product fit in
 * 64 bits. No implicit accumulator/high-product instruction or helper call. */
VPRIV val wk_udivconst(ecx *e, qty ty, val a, const em_udivconst_recipe *r)
{
	int input = wk_reg(e, &a, ty), saved = -1;
	LO(ext)(e, input, 4, false);
	if (r->remainder || r->add) {
		saved = LO(treg)(e);
		LO(mov)(e, saved, input);
	}
	val q;
	if (r->compare) {
		q = LO(cmp)(e, T_GREATEREQ, 4, false, a, vconst(r->divisor));
	} else {
		q = LO(bin)(e, T_STAR, 8, false, a, vconst(r->magic));
		q = LO(bin)(e, T_GREATERGREATER, 8, false, q,
			    vconst(r->add ? 32 : 32 + r->shift));
		if (r->add) {
			int correction = saved;
			if (r->remainder) {
				correction = LO(treg)(e);
				LO(mov)(e, correction, saved);
			}
			val n = { .k=V_REG, .r=(uint8_t)correction };
			int qr = q.r;
			n = LO(bin)(e, T_MINUS, 4, false, n, q);
			LO(tkeep)(e, qr);
			n = LO(bin)(e, T_GREATERGREATER, 4, false, n, vconst(1));
			q = LO(bin)(e, T_PLUS, 4, false, n, q);
			q = LO(bin)(e, T_GREATERGREATER, 4, false, q, vconst(r->shift));
		}
	}
	if (r->remainder) {
		q = LO(bin)(e, T_STAR, 4, false, q, vconst(r->divisor));
		q = LO(bin)(e, T_MINUS, 4, false, (val){ .k=V_REG, .r=(uint8_t)saved }, q);
	}
	return q;
}
