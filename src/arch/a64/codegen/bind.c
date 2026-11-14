/* vcc code generator: reference logging and symbol bindings */
#include "../../../codegen/codegen-internal.h"

/* ---------------- bindings ---------------- */

VPRIV int regvar_reg(cc *c, uint32_t s)
{
	const attrs *a = codegen_symbol_attributes(c, s);
	const char *n;

	if (c->syms[s].sc != AST_STORAGE_REGISTER || !a->asmname)
		return -1;
	n = c->strs[a->asmname - 1].s;
	if (!strcmp(n, "sp"))
		return SP;
	if ((n[0] == 'x' || n[0] == 'w') && n[1] >= '0' && n[1] <= '9')
		return atoi(n + 1);
	return -1;
}

VPRIV int64_t bind_slot(codegen_context *e, uint32_t s)
{
	cc *c = e->compile_context;
	codegen_binding *b = codegen_symbol_binding_get(e, s);
	ast_qualified_type t = c->syms[s].ty;
	uint32_t al = codegen_type_alignment(c, t);

	if (c->syms[s].align > al)
		al = c->syms[s].align;
	b->instance = e->function_instance->instance;
	b->kind = CODEGEN_BINDING_SLOT;
	b->known_value_index = 0;
	b->register_number = 0xff;
	if (c->syms[s].sc == AST_STORAGE_REGISTER) {
		int rr = regvar_reg(c, s);

		if (rr >= 0)
			b->register_number = (uint8_t)rr;
	}
	b->slot = a64_frame_slot_allocate(e, codegen_type_size(c, t) ? codegen_type_size(c, t) : 1, al);
	return b->slot;
}

/* the callee-saved register local s of the emitted function lives in, or -1 */
VPRIV int lreg_of(codegen_context *e, uint32_t s)
{
	for (int i = 0; i < e->local_register_candidate_count; i++)
		if (e->local_register_symbols[i] == s)
			return e->local_register_numbers[i];
	return -1;
}

/* bind s to its register (lreg_of) for this instance */
VPRIV codegen_lvalue bind_lreg(codegen_context *e, uint32_t s)
{
	codegen_binding *b = codegen_symbol_binding_get(e, s);
	codegen_lvalue l = { .kind = CODEGEN_LVALUE_LOCAL_REGISTER };

	b->instance = e->function_instance->instance;
	b->kind = CODEGEN_BINDING_LOCAL_REGISTER;
	b->known_value_index = 0;
	b->register_number = 0xff;
	b->value = codegen_value_register(lreg_of(e, s));
	l.register_number = b->value.register_number;
	l.align = codegen_type_alignment(e->compile_context, e->compile_context->syms[s].ty);
	return l;
}

/* the lvalue of a local (auto/param/register) variable */
VPRIV codegen_lvalue local_lval(codegen_context *e, uint32_t s)
{
	cc *c = e->compile_context;
	codegen_binding *b = codegen_symbol_binding_get(e, s);
	codegen_lvalue l = { 0 };
	ast_qualified_type t = c->syms[s].ty;

	l.align = codegen_type_alignment(c, t);
	if (b->kind == CODEGEN_BINDING_LOCAL_REGISTER && b->instance) {
		l.kind = CODEGEN_LVALUE_LOCAL_REGISTER;
		l.register_number = b->value.register_number;
		return l;
	}
	if ((b->kind == B_NONE || b->instance == 0) && lreg_of(e, s) >= 0)
		return bind_lreg(e, s);	/* declaration jumped over */
	if (b->kind == CODEGEN_BINDING_REGISTER && b->instance) {
		/* read-only, its address never taken: a copy in a fresh slot will do */
		l.kind = CODEGEN_LVALUE_FRAME;
		l.offset = a64_frame_slot_allocate(e, 8, 8);
		a64_record_emit_memory(e, A64_OP_ST, 3, 0, b->value.register_number, A64_REGISTER_FRAME, l.offset);
		return l;
	}
	if (b->kind == B_NONE || b->instance == 0) {
		/* declaration not executed (jumped over, or dead): give it a slot */
		bind_slot(e, s);
	} else if (b->kind == CODEGEN_BINDING_CONSTANT) {
		codegen_value v = b->value;
		int r;

		bind_slot(e, s);
		if (codegen_type_is_wide(c, t)) {
			toreg2(e, &v, codegen_type_is_signed(c, t));
			a64_record_emit_memory(e, A64_OP_ST, 3, 0, v.register_number, A64_REGISTER_FRAME, b->slot);
			a64_record_emit_memory(e, A64_OP_ST, 3, 0, v.register_high, A64_REGISTER_FRAME, b->slot + 8);
		} else {
			r = toreg(e, &v);
			a64_record_emit_memory(e, A64_OP_ST, a64_size_log2(codegen_type_size(c, t)), 0, r, A64_REGISTER_FRAME, b->slot);
		}
		val_free(e, &v);
	}
	if (b->kind == CODEGEN_BINDING_INDIRECT) {
		int r = treg(e);

		a64_record_emit_memory(e, A64_OP_LD, 3, 0, r, A64_REGISTER_FRAME, b->slot);
		l.kind = CODEGEN_LVALUE_REGISTER;
		l.register_number = (uint8_t)r;
		return l;
	}
	l.kind = CODEGEN_LVALUE_FRAME;
	l.offset = b->slot;
	return l;
}
