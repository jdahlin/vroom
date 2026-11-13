/* vcc code generator: temporaries, frame slots, values, lvalues */
#include "../../../codegen/codegen-internal.h"

/* ---------------- temporaries and frame slots ---------------- */

VPRIV int treg(codegen_context *e)
{
	for (int i = 0; i < CODEGEN_TEMPORARY_COUNT; i++)
		if (!((e->live | e->resv) & (1u << i))) {
			e->live |= 1u << i;
			if (codegen_temporary_registers[i] >= 19)
				e->usedcs |= 1u << codegen_temporary_registers[i];
			return codegen_temporary_registers[i];
		}
	if (e->nlr || (e->inlfree && e->inlfree < CODEGEN_INLINE_FREE_SAFE) || !e->spill)
		_longjmp(e->bail, 2);	/* emit_function tries again more carefully */
	{
		const codegen_function_instance *f = e->f;

		while (f->up)
			f = f->up;
		if (getenv("VCC_LEAKCHK"))
			codegen_diagnostic_report(e, e->curloc, "here");
		codegen_fatal_report(e, e->c->syms[f->fn].loc, "expression too complex (out of registers) in '%.*s'",
		       (int)vintern_ents[e->c->syms[f->fn].name].len, vintern_ents[e->c->syms[f->fn].name].s);
	}
}


VPRIV void tfree(codegen_context *e, int r)
{
	int i = codegen_temporary_index(r);

	if (i >= 0)
		e->live &= ~(1u << i);
}

VPRIV void val_free(codegen_context *e, const codegen_value *v)
{
	if (v->k == CODEGEN_VALUE_REGISTER)
		tfree(e, v->r);
	else if (v->k == CODEGEN_VALUE_REGISTER_PAIR)
		tfree(e, v->r), tfree(e, v->r2);
}

VPRIV bool codegen_symbol_needs_got(cc *c, uint32_t ref);

/* value into a (new) register; constants are materialised */
VPRIV int toreg(codegen_context *e, codegen_value *v)
{
	int r;

	switch (v->k) {
	case CODEGEN_VALUE_REGISTER:
		return v->r;
	case CODEGEN_VALUE_REGISTER_PAIR:
		tfree(e, v->r2);
		*v = codegen_value_register(v->r);
		return v->r;
	case CODEGEN_VALUE_CONSTANT:
		r = treg(e);
		movi(e, 1, r, (int64_t)v->i);
		*v = codegen_value_register(r);
		return r;
	case CODEGEN_VALUE_ADDRESS:
		r = treg(e);
		addr_of_ref(e, r, v->ref, (int64_t)v->i);
		*v = codegen_value_register(r);
		return r;
	default:
		r = treg(e);
		movi(e, 1, r, 0);
		*v = codegen_value_register(r);
		return r;
	}
}

/* 128-bit value into a register pair */
VPRIV void toreg2(codegen_context *e, codegen_value *v, bool sgn)
{
	if (v->k == CODEGEN_VALUE_REGISTER_PAIR)
		return;
	if (v->k == CODEGEN_VALUE_CONSTANT) {
		int lo = treg(e), hi = treg(e);

		movi(e, 1, lo, (int64_t)(uint64_t)(u128)v->i);
		movi(e, 1, hi, (int64_t)(uint64_t)((u128)v->i >> 64));
		v->k = CODEGEN_VALUE_REGISTER_PAIR, v->r = (uint8_t)lo, v->r2 = (uint8_t)hi;
		return;
	}
	{
		int lo = toreg(e, v), hi = treg(e);

		if (sgn)
			opbf(e, I_SBFM, 1, hi, lo, 63, 63);
		else
			movi(e, 1, hi, 0);
		v->k = CODEGEN_VALUE_REGISTER_PAIR, v->r = (uint8_t)lo, v->r2 = (uint8_t)hi;
	}
}

VPRIV void lfree(codegen_context *e, const codegen_lvalue *l)
{
	if (l->k == CODEGEN_LVALUE_REGISTER)
		tfree(e, l->r);
}

/* address of an lvalue into a new register (or a constant address) */
VPRIV codegen_value laddr(codegen_context *e, codegen_lvalue *l)
{
	int r;

	switch (l->k) {
	case CODEGEN_LVALUE_SYMBOL:
		if (unlikely(codegen_reference_is_weak(e->c, l->ref))) {
			r = treg(e);
			addr_of_ref(e, r, l->ref, l->off);
			return codegen_value_register(r);
		}
		return codegen_value_address(l->ref, l->off);
	case CODEGEN_LVALUE_FRAME:
		r = treg(e);
		opi(e, I_ADDI, 1, r, FRAME, l->off);
		return codegen_value_register(r);
	case CODEGEN_LVALUE_REGISTER:
		if (l->off)
			opi(e, I_ADDI, 1, l->r, l->r, l->off);
		l->off = 0;
		return codegen_value_register(l->r);
	case CODEGEN_LVALUE_LOCAL_REGISTER:
		codegen_fatal_report(e, e->curloc, "internal: address of a register variable");
	default:
		r = treg(e);
		mov(e, 1, r, l->r);
		return codegen_value_register(r);
	}
}

/* lvalue from an address value */
VPRIV codegen_lvalue lfromval(codegen_context *e, codegen_value v, uint32_t align)
{
	codegen_lvalue l = { 0 };

	l.align = align;
	if (v.k == CODEGEN_VALUE_ADDRESS) {
		l.k = CODEGEN_LVALUE_SYMBOL;
		l.ref = v.ref;
		l.off = (int64_t)v.i;
		return l;
	}
	l.k = CODEGEN_LVALUE_REGISTER;
	l.r = (uint8_t)toreg(e, &v);
	return l;
}

VPRIV void copy_mem(codegen_context *e, int dst, int src, uint64_t n)
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
	call_fixed(e, CODEGEN_LIBRARY_MEMCPY, dst, src, -1, (int64_t)n);
}

VPRIV void zero_mem(codegen_context *e, int dst, uint64_t n)
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
	call_fixed(e, CODEGEN_LIBRARY_MEMSET, dst, -2, -1, (int64_t)n);
}
