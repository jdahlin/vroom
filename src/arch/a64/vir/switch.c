/* arm64 lowering of VIR: the switch instruction. */

#include "lowering-internal.h"

VPRIV void a64_vir_switch_lower(vl *L, uint32_t b, const vir_insn *in)
{
	ecx *e = L->e;
	const vir_function *f = L->f;
	int x = a64_vir_value_is_64_bit(f, in->a), r = a64_vir_operand_general_register(L, 0, R_X16);
	uint32_t n = f->extra[in->c], stride = x ? 5 : 3;
	const uint32_t *cw = &f->extra[in->c + 1];
	uint32_t dl = a64_vir_successor_label(L, b, 0, in->b);
	uint64_t span = 0, lo = 0, cnt = 0;

	if (n) {
		lo = x ? (uint64_t)cw[1] << 32 | cw[0] : cw[0];
		uint64_t hi = x ? (uint64_t)cw[stride * (n - 1) + 3] << 32 | cw[stride * (n - 1) + 2] : cw[stride * (n - 1) + 1];

		span = hi - lo;
		for (uint32_t k = 0; k < n; k++) {
			uint64_t a = x ? (uint64_t)cw[stride * k + 1] << 32 | cw[stride * k] : cw[stride * k];
			uint64_t h = x ? (uint64_t)cw[stride * k + 3] << 32 | cw[stride * k + 2] : cw[stride * k + 1];

			cnt += h - a >= 64 ? 64 : h - a + 1;	/* (h - a + 1 wraps to 0 for a range of all 2^64 values) */
		}
	}
	if (n >= 6 && span < 4096 && span + 1 <= 4 * cnt && !e->E->noopt) {
		uint32_t tab = newlabel(e), off = e->njtl;
		int ix = r;

		/* the index: the selector itself from 0 (a word through uxtw), else x17; never x16,
		 * which expand_tables' adr writes first, nor zr */
		if (lo)
			opi(e, I_SUBI, x, ix = R_X17, r, (int64_t)lo);
		else if (r == R_X16 || r >= 31)
			op3(e, I_MOV, x, ix = R_X17, r, 0);
		opi(e, I_SUBSI, x, ZR, ix, (int64_t)span);
		bcond(e, C_HI, dl);
		if (e->njtl + (uint32_t)span + 1 > e->capjtl) {
			e->capjtl = (e->njtl + (uint32_t)span + 1) * 2;
			e->jtl = realloc(e->jtl, (size_t)e->capjtl * sizeof *e->jtl);
		}
		if (e->njt == e->capjt) {
			e->capjt = e->capjt ? e->capjt * 2 : 4;
			e->jt = realloc(e->jt, (size_t)e->capjt * sizeof *e->jt);
		}
		if (!e->jtl || !e->jt)
			abort();
		uint32_t k = 0, l = 0;

		for (uint64_t v = lo; v <= lo + span; v++) {
			uint64_t h;

			while (k < n && (h = x ? (uint64_t)cw[stride * k + 3] << 32 | cw[stride * k + 2] : cw[stride * k + 1]) < v)
				k++, l = 0;
			uint64_t a = k < n ? (x ? (uint64_t)cw[stride * k + 1] << 32 | cw[stride * k] : cw[stride * k]) : 0;
			uint32_t t;

			if (k < n && a <= v) {
				if (!l)
					l = a64_vir_successor_label(L, b, 1 + k, cw[stride * k + stride - 1]);
				t = l;
			} else {
				t = dl;
			}
			e->jtl[e->njtl++] = t;
			if (!(e->lrefc[t] & LREF_EXT)) {
				e->lrefc[t]++;
				e->lrefc[t] |= LREF_EXT;
			}
		}
		e->jt[e->njt++] = (struct ejt){ tab, off, (uint32_t)span + 1 };
		ins *i = put(e, I_JT);
		i->rd = (uint8_t)ix;
		i->cond = ix != R_X17 && !x ? 2 : 0;
		i->label = tab;
		e->reach = false;
		return;
	}
	for (uint32_t k = 0; k < n; k++) {
		uint64_t a = x ? (uint64_t)cw[stride * k + 1] << 32 | cw[stride * k] : cw[stride * k];
		uint64_t h = x ? (uint64_t)cw[stride * k + 3] << 32 | cw[stride * k + 2] : cw[stride * k + 1];
		uint32_t t = a64_vir_successor_label(L, b, 1 + k, cw[stride * k + stride - 1]);

		if (a == h) {
			opi(e, I_SUBSI, x, ZR, r, x ? (int64_t)a : (int64_t)(int32_t)a);
			bcond(e, C_EQ, t);
		} else {
			if (r == R_X17)
				a64_vir_fail(L, VIRBUILD_FAILURE(switch_lowering));
			opi(e, I_SUBI, x, R_X17, r, (int64_t)a);
			opi(e, I_SUBSI, x, ZR, R_X17, (int64_t)(h - a));
			bcond(e, C_LS, t);
		}
	}
	jump(e, dl);
}
