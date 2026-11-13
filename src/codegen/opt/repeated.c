/*
 * repeated.c: identities for two reads of the same plain local integer: x - x, x ^ x, x == x,
 * x < x and the like.
 *
 * em_repeated_integer(e, n) returns an EM_ID_* recipe (the result is zero, true, or unknown).  Only
 * for an ordinary automatic variable of 1 to 8 bytes: not static or extern, address never taken, not
 * volatile or atomic, no asm name.  Anything that another thread or a signal handler could change
 * is excluded by those rules.  -O1 and up.  Used by the shared walker, arm64's expression code and
 * wasm's emitter.
 */
#include "../emitimpl.h"

VPRIV int em_repeated_integer(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n), *a, *b;
	const sym *s;
	qty t;
	uint64_t size;

	if (!e->E->o->opt || e->E->noopt || x->op != N_BINARY ||
	    (x->aux != LEX_TOKEN_MINUS && x->aux != LEX_TOKEN_CARET && x->aux != LEX_TOKEN_AMPERSAND && x->aux != LEX_TOKEN_PIPE &&
	     x->aux != LEX_TOKEN_EQUAL_EQUAL && x->aux != LEX_TOKEN_EXCLAMATION_EQUAL && x->aux != LEX_TOKEN_LESS &&
	     x->aux != LEX_TOKEN_GREATER && x->aux != LEX_TOKEN_LESS_EQUAL && x->aux != LEX_TOKEN_GREATER_EQUAL))
		return EM_ID_NONE;
	a = N(x->a); b = N(x->b);
	if (a->op != N_VAR || b->op != N_VAR || a->a != b->a || QUNQ(a->ty) != QUNQ(b->ty) ||
	    !is_integer(c, a->ty) || em_bitint(c, a->ty))
		return EM_ID_NONE;
	s = &c->syms[a->a];
	t = s->ty;
	size = tsize(c, t);
	if (!is_local_var(c, a->a) || s->kind != S_VAR || s->sc == SC_STATIC || s->sc == SC_EXTERN ||
	    (s->flags & SF_ADDR_TAKEN) || (QQ(t) & Q_VOLATILE) || (T(c, t)->flags & TF_ATOMIC) ||
	    !size || size > 8 || sym_attrs(c, a->a)->asmname)
		return EM_ID_NONE;
	switch (x->aux) {
	case LEX_TOKEN_MINUS: case LEX_TOKEN_CARET: case LEX_TOKEN_EXCLAMATION_EQUAL: case LEX_TOKEN_LESS: case LEX_TOKEN_GREATER:
		return EM_ID_ZERO;
	case LEX_TOKEN_EQUAL_EQUAL: case LEX_TOKEN_LESS_EQUAL: case LEX_TOKEN_GREATER_EQUAL:
		return EM_ID_TRUE;
	default:
		return EM_ID_LEFT;
	}
}
