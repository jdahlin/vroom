/* vcc code generator: temporaries, frame slots, values, lvalues */
#include "../../../codegen/emitimpl.h"

/* ---------------- temporaries and frame slots ---------------- */

VPRIV int treg(ecx *e)
{
	for (int i = 0; i < NTEMP; i++)
		if (!((e->live | e->resv) & (1u << i))) {
			e->live |= 1u << i;
			if (tempregs[i] >= 19)
				e->usedcs |= 1u << tempregs[i];
			return tempregs[i];
		}
	if (e->nlr || (e->inlfree && e->inlfree < INLINE_FREE_SAFE) || !e->spill)
		_longjmp(e->bail, 2);	/* emit_function tries again more carefully */
	{
		const fnctx *f = e->f;

		while (f->up)
			f = f->up;
		if (getenv("VCC_LEAKCHK"))
			ediag(e, e->curloc, "here");
		efatal(e, e->c->syms[f->fn].loc, "expression too complex (out of registers) in '%.*s'",
		       (int)vintern_ents[e->c->syms[f->fn].name].len, vintern_ents[e->c->syms[f->fn].name].s);
	}
}


VPRIV void tfree(ecx *e, int r)
{
	int i = tidx(r);

	if (i >= 0)
		e->live &= ~(1u << i);
}

VPRIV void val_free(ecx *e, const val *v)
{
	if (v->k == V_REG)
		tfree(e, v->r);
	else if (v->k == V_REG2)
		tfree(e, v->r), tfree(e, v->r2);
}

VPRIV bool sym_needs_got(cc *c, uint32_t ref);

/* value into a (new) register; constants are materialised */
VPRIV int toreg(ecx *e, val *v)
{
	int r;

	switch (v->k) {
	case V_REG:
		return v->r;
	case V_REG2:
		tfree(e, v->r2);
		*v = vreg(v->r);
		return v->r;
	case V_CONST:
		r = treg(e);
		movi(e, 1, r, (int64_t)v->i);
		*v = vreg(r);
		return r;
	case V_ADDR:
		r = treg(e);
		addr_of_ref(e, r, v->ref, (int64_t)v->i);
		*v = vreg(r);
		return r;
	default:
		r = treg(e);
		movi(e, 1, r, 0);
		*v = vreg(r);
		return r;
	}
}

/* 128-bit value into a register pair */
VPRIV void toreg2(ecx *e, val *v, bool sgn)
{
	if (v->k == V_REG2)
		return;
	if (v->k == V_CONST) {
		int lo = treg(e), hi = treg(e);

		movi(e, 1, lo, (int64_t)(uint64_t)(u128)v->i);
		movi(e, 1, hi, (int64_t)(uint64_t)((u128)v->i >> 64));
		v->k = V_REG2, v->r = (uint8_t)lo, v->r2 = (uint8_t)hi;
		return;
	}
	{
		int lo = toreg(e, v), hi = treg(e);

		if (sgn)
			opbf(e, I_SBFM, 1, hi, lo, 63, 63);
		else
			movi(e, 1, hi, 0);
		v->k = V_REG2, v->r = (uint8_t)lo, v->r2 = (uint8_t)hi;
	}
}

VPRIV void lfree(ecx *e, const lval *l)
{
	if (l->k == L_REG)
		tfree(e, l->r);
}

/* address of an lvalue into a new register (or a constant address) */
VPRIV val laddr(ecx *e, lval *l)
{
	int r;

	switch (l->k) {
	case L_SYM:
		if (unlikely(ref_weak(e->c, l->ref))) {
			r = treg(e);
			addr_of_ref(e, r, l->ref, l->off);
			return vreg(r);
		}
		return vaddr(l->ref, l->off);
	case L_FRAME:
		r = treg(e);
		opi(e, I_ADDI, 1, r, FRAME, l->off);
		return vreg(r);
	case L_REG:
		if (l->off)
			opi(e, I_ADDI, 1, l->r, l->r, l->off);
		l->off = 0;
		return vreg(l->r);
	case L_VREG:
		efatal(e, e->curloc, "internal: address of a register variable");
	default:
		r = treg(e);
		mov(e, 1, r, l->r);
		return vreg(r);
	}
}

/* lvalue from an address value */
VPRIV lval lfromval(ecx *e, val v, uint32_t align)
{
	lval l = { 0 };

	l.align = align;
	if (v.k == V_ADDR) {
		l.k = L_SYM;
		l.ref = v.ref;
		l.off = (int64_t)v.i;
		return l;
	}
	l.k = L_REG;
	l.r = (uint8_t)toreg(e, &v);
	return l;
}

VPRIV void copy_mem(ecx *e, int dst, int src, uint64_t n)
{
	if (n <= 128) {
		uint64_t o = 0;
		int t = R_X16;	/* not x17: the backend uses it for large offsets */

		while (o < n) {
			uint64_t k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;

			mem(e, I_LD, log2sz(k), 0, t, src, (int64_t)o);
			mem(e, I_ST, log2sz(k), 0, t, dst, (int64_t)o);
			o += k;
		}
		return;
	}
	call_fixed(e, FN_MEMCPY, dst, src, -1, (int64_t)n);
}

VPRIV void zero_mem(ecx *e, int dst, uint64_t n)
{
	if (n <= 256) {
		uint64_t o = 0;

		while (o < n) {
			uint64_t k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;

			mem(e, I_ST, log2sz(k), 0, ZR, dst, (int64_t)o);
			o += k;
		}
		return;
	}
	call_fixed(e, FN_MEMSET, dst, -2, -1, (int64_t)n);
}
