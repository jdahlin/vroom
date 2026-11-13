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

/* Unsigned32 uses a complete widened product. Unsigned64 asks the target for
 * the high half, including preservation of any implicit machine registers. */
VPRIV val wk_udivconst(ecx *e, qty ty, val a, const em_udivconst_recipe *r)
{
	int size = r->bits / 8, input = wk_reg(e, &a, ty), saved = -1;
	if (r->bits == 32)
		LO(ext)(e, input, 4, false);
	if (r->remainder || r->add) {
		saved = LO(treg)(e);
		LO(mov)(e, saved, input);
	}
	val q;
	if (r->compare) {
		q = LO(cmp)(e, LEX_TOKEN_GREATER_EQUAL, size, false, a, vconst(r->divisor));
	} else {
		if (r->bits == 64) {
			q = LO(umulhi)(e, a, vconst(r->magic));
			if (!r->add && r->shift)
				q = LO(bin)(e, LEX_TOKEN_GREATER_GREATER, 8, false, q, vconst(r->shift));
		} else {
			q = LO(bin)(e, LEX_TOKEN_STAR, 8, false, a, vconst(r->magic));
			q = LO(bin)(e, LEX_TOKEN_GREATER_GREATER, 8, false, q,
				    vconst(r->add ? 32 : 32 + r->shift));
		}
		if (r->add) {
			int correction = saved;
			if (r->remainder) {
				correction = LO(treg)(e);
				LO(mov)(e, correction, saved);
			}
			val n = { .k=V_REG, .r=(uint8_t)correction };
			int qr = q.r;
			n = LO(bin)(e, LEX_TOKEN_MINUS, size, false, n, q);
			LO(tkeep)(e, qr);
			n = LO(bin)(e, LEX_TOKEN_GREATER_GREATER, size, false, n, vconst(1));
			q = LO(bin)(e, LEX_TOKEN_PLUS, size, false, n, q);
			q = LO(bin)(e, LEX_TOKEN_GREATER_GREATER, size, false, q, vconst(r->shift));
		}
	}
	if (r->remainder) {
		q = LO(bin)(e, LEX_TOKEN_STAR, size, false, q, vconst(r->divisor));
		q = LO(bin)(e, LEX_TOKEN_MINUS, size, false, (val){ .k=V_REG, .r=(uint8_t)saved }, q);
	}
	return q;
}
