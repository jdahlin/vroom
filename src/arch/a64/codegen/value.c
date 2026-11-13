/* vcc code generator: temporaries, frame slots, values, lvalues */
#include "../../../codegen/codegen-internal.h"

/* ---------------- temporaries and frame slots ---------------- */

VPRIV int treg(codegen_context *e)
{
	for (int i = 0; i < CODEGEN_TEMPORARY_COUNT; i++)
		if (!((e->temporaries_in_use | e->temporaries_reserved) & (1u << i))) {
			e->temporaries_in_use |= 1u << i;
			if (codegen_temporary_registers[i] >= 19)
				e->callee_saved_used |= 1u << codegen_temporary_registers[i];
			return codegen_temporary_registers[i];
		}
	if (e->local_register_candidate_count || (e->inline_free_temporaries && e->inline_free_temporaries < CODEGEN_INLINE_FREE_SAFE) || !e->spill)
		_longjmp(e->bail_jump, 2);	/* emit_function tries again more carefully */
	{
		const codegen_function_instance *f = e->function_instance;

		while (f->outer)
			f = f->outer;
		if (getenv("VCC_LEAKCHK"))
			codegen_diagnostic_report(e, e->current_location, "here");
		codegen_fatal_report(e, e->compile_context->syms[f->function].loc, "expression too complex (out of registers) in '%.*s'",
		       (int)vintern_ents[e->compile_context->syms[f->function].name].len, vintern_ents[e->compile_context->syms[f->function].name].s);
	}
}


VPRIV void tfree(codegen_context *e, int r)
{
	int i = codegen_temporary_index(r);

	if (i >= 0)
		e->temporaries_in_use &= ~(1u << i);
}

VPRIV void val_free(codegen_context *e, const codegen_value *v)
{
	if (v->kind == CODEGEN_VALUE_REGISTER)
		tfree(e, v->register_number);
	else if (v->kind == CODEGEN_VALUE_REGISTER_PAIR)
		tfree(e, v->register_number), tfree(e, v->register_high);
}

VPRIV bool codegen_symbol_needs_got(cc *c, uint32_t ref);

/* value into a (new) register; constants are materialised */
VPRIV int toreg(codegen_context *e, codegen_value *v)
{
	int r;

	switch (v->kind) {
	case CODEGEN_VALUE_REGISTER:
		return v->register_number;
	case CODEGEN_VALUE_REGISTER_PAIR:
		tfree(e, v->register_high);
		*v = codegen_value_register(v->register_number);
		return v->register_number;
	case CODEGEN_VALUE_CONSTANT:
		r = treg(e);
		movi(e, 1, r, (int64_t)v->integer);
		*v = codegen_value_register(r);
		return r;
	case CODEGEN_VALUE_ADDRESS:
		r = treg(e);
		addr_of_ref(e, r, v->reference, (int64_t)v->integer);
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
	if (v->kind == CODEGEN_VALUE_REGISTER_PAIR)
		return;
	if (v->kind == CODEGEN_VALUE_CONSTANT) {
		int lo = treg(e), hi = treg(e);

		movi(e, 1, lo, (int64_t)(uint64_t)(u128)v->integer);
		movi(e, 1, hi, (int64_t)(uint64_t)((u128)v->integer >> 64));
		v->kind = CODEGEN_VALUE_REGISTER_PAIR, v->register_number = (uint8_t)lo, v->register_high = (uint8_t)hi;
		return;
	}
	{
		int lo = toreg(e, v), hi = treg(e);

		if (sgn)
			opbf(e, I_SBFM, 1, hi, lo, 63, 63);
		else
			movi(e, 1, hi, 0);
		v->kind = CODEGEN_VALUE_REGISTER_PAIR, v->register_number = (uint8_t)lo, v->register_high = (uint8_t)hi;
	}
}

VPRIV void lfree(codegen_context *e, const codegen_lvalue *l)
{
	if (l->kind == CODEGEN_LVALUE_REGISTER)
		tfree(e, l->register_number);
}

/* address of an lvalue into a new register (or a constant address) */
VPRIV codegen_value laddr(codegen_context *e, codegen_lvalue *l)
{
	int r;

	switch (l->kind) {
	case CODEGEN_LVALUE_SYMBOL:
		if (unlikely(codegen_reference_is_weak(e->compile_context, l->reference))) {
			r = treg(e);
			addr_of_ref(e, r, l->reference, l->offset);
			return codegen_value_register(r);
		}
		return codegen_value_address(l->reference, l->offset);
	case CODEGEN_LVALUE_FRAME:
		r = treg(e);
		opi(e, I_ADDI, 1, r, FRAME, l->offset);
		return codegen_value_register(r);
	case CODEGEN_LVALUE_REGISTER:
		if (l->offset)
			opi(e, I_ADDI, 1, l->register_number, l->register_number, l->offset);
		l->offset = 0;
		return codegen_value_register(l->register_number);
	case CODEGEN_LVALUE_LOCAL_REGISTER:
		codegen_fatal_report(e, e->current_location, "internal: address of a register variable");
	default:
		r = treg(e);
		mov(e, 1, r, l->register_number);
		return codegen_value_register(r);
	}
}

/* lvalue from an address value */
VPRIV codegen_lvalue lfromval(codegen_context *e, codegen_value v, uint32_t align)
{
	codegen_lvalue l = { 0 };

	l.align = align;
	if (v.kind == CODEGEN_VALUE_ADDRESS) {
		l.kind = CODEGEN_LVALUE_SYMBOL;
		l.reference = v.reference;
		l.offset = (int64_t)v.integer;
		return l;
	}
	l.kind = CODEGEN_LVALUE_REGISTER;
	l.register_number = (uint8_t)toreg(e, &v);
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
