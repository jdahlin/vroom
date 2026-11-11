/* Identities for two reads of the same ordinary automatic integer. */
#include "../emitimpl.h"

VPRIV int em_repeated_integer(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n), *a, *b;
	const sym *s;
	qty t;
	uint64_t size;

	if (!e->E->o->opt || e->E->noopt || x->op != N_BINARY ||
	    (x->aux != T_MINUS && x->aux != T_CARET && x->aux != T_AMP && x->aux != T_PIPE))
		return EM_ID_NONE;
	a = N(x->a); b = N(x->b);
	if (a->op != N_VAR || b->op != N_VAR || a->a != b->a || QUNQ(a->ty) != QUNQ(x->ty) ||
	    QUNQ(b->ty) != QUNQ(x->ty) || !is_integer(c, x->ty) || em_bitint(c, x->ty))
		return EM_ID_NONE;
	s = &c->syms[a->a];
	t = s->ty;
	size = tsize(c, t);
	if (!is_local_var(c, a->a) || s->kind != S_VAR || s->sc == SC_STATIC || s->sc == SC_EXTERN ||
	    (s->flags & SF_ADDR_TAKEN) || (QQ(t) & Q_VOLATILE) || (T(c, t)->flags & TF_ATOMIC) ||
	    !size || size > 8 || sym_attrs(c, a->a)->asmname)
		return EM_ID_NONE;
	return x->aux == T_MINUS || x->aux == T_CARET ? EM_ID_ZERO : EM_ID_LEFT;
}
