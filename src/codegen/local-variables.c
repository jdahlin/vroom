/*
 * local-variables.c: the emitter's per-symbol binding table (binding) and the one definition of
 * "this symbol is a local variable" (codegen_symbol_is_local_variable).
 *
 * codegen_symbol_is_local_variable is true for automatic and register variables, parameters, and block-scope symbols with
 * no storage class that are not marked global; a static or extern local is not one, because it lives
 * in the data section.  Every backend and optimizer asks it, so a change here changes what gets a
 * register or a frame slot everywhere.
 */
#include "codegen-internal.h"

VPRIV codegen_binding *codegen_symbol_binding_get(codegen_context *e, uint32_t s)
{
	return &e->b[s];
}

VPRIV bool codegen_symbol_is_local_variable(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];

	return y->kind == AST_SYMBOL_VARIABLE && y->depth > 0 && (y->sc == AST_STORAGE_AUTO || y->sc == AST_STORAGE_REGISTER || (y->flags & AST_SYMBOL_FLAG_PARAMETER) ||
						   (y->sc == AST_STORAGE_NONE && !(y->flags & AST_SYMBOL_FLAG_GLOBAL)));
}
