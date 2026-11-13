/* x86 persistent scalar homes. Selection and escape policy live in opt/locals.c;
   this file only supplies physical homes and preserves the System V ABI. */
#include "x64-internal.h"

/* A home adds a save and restore; a stack parameter also needs an entry load.
   Require at least four weighted references to repay that memory traffic.
   Loop references already carry the shared prepass's loop weight. */
static bool x64_local_profitable(codegen_context *e, uint32_t s)
{
	cc *c = e->compile_context;
	const sym *y = &c->syms[s];
	cc_value cv;

	if (e->unit->local_use_weights[s] < 4)
		return false;
	/* walk_declaration_emit binds these constants without any storage. Reserving a physical
	   home would add callee saves while removing no memory accesses. */
	if (!(y->flags & AST_SYMBOL_FLAG_PARAMETER) && y->sc != AST_STORAGE_REGISTER && y->body &&
	    !(e->unit->symbol_flags[s] & (CODEGEN_SYMBOL_WRITTEN | CODEGEN_SYMBOL_CONSTANT_WRITTEN)) &&
	    !codegen_expression_has_side_effects(c, y->body) && cc_eval(c, y->body, &cv) && cv.kind == CC_VALUE_INTEGER)
		return false;
	return true;
}

/* Callee saves share the printer's prologue slots with inline asm, including
   CFI for normal and realigned frames. Asm separately snapshots current home
   values across its fixed operands/clobbers and every goto edge. */
VPRIV void x64_locals_begin(codegen_context *e, uint32_t s)
{
	static const uint8_t regs[] = { RBX, R13, R14, R15 };
	x64_lowering_context *X = e->x64_context;
	unsigned n = sizeof regs;

	X->local_register_count = 0;
	if (!e->unit->options->opt || e->unit->cleanups_disabled || !e->unit->local_register_count)
		return;
	if (n > (unsigned)e->unit->local_register_count) n = (unsigned)e->unit->local_register_count;
	unsigned selected = em_select_locals(e, s, X->local_symbols, n, em_local_gpr_type);
	/* At most four profitability checks; no second walk or candidate buffer.
	   Filtered homes remain unused; refilling can move into shared selection
	   if measurements justify a richer profitability interface. */
	for (unsigned i = 0; i < selected; i++)
		if (x64_local_profitable(e, X->local_symbols[i]))
			X->local_symbols[X->local_register_count++] = X->local_symbols[i];
	for (unsigned i = 0; i < X->local_register_count; i++) {
		X->local_registers[i] = regs[i];
		if (!X->callee_save_slots[X->local_registers[i]])
			X->callee_save_slots[X->local_registers[i]] = walk_slot_permanent(e, 8, 8);
	}
}

/* Nothing to do for the direct x86-64 lowering.  The VIR target (PLAN-vir.md,
   SPEC-vir.md X2) makes a slot object per hand-out, so that an access to a
   reused frame offset resolves to the object that owns it at that point; a
   shared cell analysis (codegen/rec/slots.h) could take its identities here. */
VPRIV void x64_slot(codegen_context *e, int64_t off, uint64_t size, uint32_t align, int kind)
{
	(void)e, (void)off, (void)size, (void)align, (void)kind;
}

VPRIV bool x64_local(codegen_context *e, uint32_t s, codegen_lvalue *l)
{
	x64_lowering_context *X = e->x64_context;

	/* An inlined body can refer to the same symbol in recursive expansion.
	   Its independent bindings must remain in the inliner's frame slots. */
	if (e->walk_state->instance)
		return false;
	for (unsigned i = 0; i < X->local_register_count; i++)
		if (X->local_symbols[i] == s) {
			*l = (codegen_lvalue){ .kind = CODEGEN_LVALUE_LOCAL_REGISTER, .register_number = X->local_registers[i], .align = codegen_symbol_alignment(e->compile_context, s) };
			return true;
		}
	return false;
}
