/* vcc code generator: symbol bindings and which symbols are local variables */
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
