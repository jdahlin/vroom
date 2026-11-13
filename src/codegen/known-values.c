/* vcc code generator: facts the walker tracks about values */
#include "emitimpl.h"

/* the kv slot of a[CONST] (create: allocate one), or -1 */
VPRIV int ael_slot(ecx *e, uint32_t n, bool create)
{
	cc *c = e->c;
	uint32_t s, idx;
	const sym *y;
	const ctype *at;
	qty et;

	if (!is_aconst(c, n))
		return -1;
	s = N(N(N(n)->a)->a)->a, y = &c->syms[s];
	if (!is_local_var(c, s) || y->sc == SC_STATIC || (e->E->sflags[s] & SA_AESC) || sym_attrs(c, s)->cleanup)
		return -1;
	at = T(c, y->ty), et = at->base;
	if (at->kind != TY_ARRAY || (at->flags & (TF_VLA | TF_INCOMPLETE)) || !is_integer(c, et) || is_wide(c, et) ||
	    (QQ(et) & Q_VOLATILE) || (T(c, et)->flags & TF_ATOMIC) || (QQ(y->ty) & Q_VOLATILE))
		return -1;
	idx = aconst_idx(c, n);
	if (idx >= at->n)
		return -1;
	for (int i = 0; i < e->nael; i++)
		if (e->ael[i].s == s && e->ael[i].idx == idx)
			return e->ael[i].kv;
	if (!create || e->nkv == NKV || e->nael == NKV)
		return -1;
	e->ael[e->nael].s = s, e->ael[e->nael].idx = idx, e->ael[e->nael].kv = e->nkv;
	e->nael++;
	e->kvalid &= (uint16_t)~(1u << e->nkv);
	return e->nkv++;
}
