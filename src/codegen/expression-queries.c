/*
 * expression-queries.c: small questions about the shape of an expression that more than one
 * backend asks.
 *
 * direct_callee finds the callee symbol of a call through casts, and returns 0 for builtins (setting
 * *ref to their name reference) and for calls through a cast to a different function type, which
 * must stay indirect.  Both functions are used by the walker, arm64's call lowering and the
 * prepass; they read the AST only.
 */
#include "emitimpl.h"

VPRIV bool ptr_to_vla(cc *c, qty ptr)
{
	const ctype *b = T(c, T(c, ptr)->base);

	return b->kind == TY_ARRAY && (b->flags & TF_VLA);
}

/* the callee sym of a direct call, or 0 */
VPRIV uint32_t direct_callee(cc *c, uint32_t fn, uint32_t *ref)
{
	const node *f = N(fn);

	while (f->op == N_CAST && (f->aux == CK_FDECAY || f->aux == CK_NOP || f->aux == CK_P2P) &&
	       (N(f->a)->op == N_VAR || N(f->a)->op == N_CAST))
		f = N(f->a);
	if (f->op != N_VAR)
		return 0;
	if (c->syms[f->a].kind == S_BUILTIN) {
		*ref = REF(RK_NAME, (uint32_t)c->syms[f->a].val);
		return 0;
	}
	if (c->syms[f->a].kind != S_FUNC)
		return 0;
	/* calls through a cast to another function type stay direct only if the types agree */
	if (N(fn)->op == N_CAST && N(N(fn)->a)->op == N_CAST)
		return 0;
	*ref = REF(RK_SYM, canon_sym(c, f->a));
	return canon_sym(c, f->a);
}
