/*
 * local-variables.c: the emitter's per-symbol binding table (binding) and the one definition of
 * "this symbol is a local variable" (is_local_var).
 *
 * is_local_var is true for automatic and register variables, parameters, and block-scope symbols with
 * no storage class that are not marked global; a static or extern local is not one, because it lives
 * in the data section.  Every backend and optimizer asks it, so a change here changes what gets a
 * register or a frame slot everywhere.
 */
#include "emitimpl.h"

VPRIV bind *binding(ecx *e, uint32_t s)
{
	return &e->b[s];
}

VPRIV bool is_local_var(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];

	return y->kind == S_VAR && y->depth > 0 && (y->sc == SC_AUTO || y->sc == SC_REGISTER || (y->flags & SF_PARAM) ||
						   (y->sc == SC_NONE && !(y->flags & SF_GLOBAL)));
}
