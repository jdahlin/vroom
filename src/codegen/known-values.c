/*
 * known-values.c: the walker's slots for constant array elements: `a[K]` of a local integer array
 * used only with constant indices (ael_slot).
 *
 * The walker tracks the value of such an element as it does a variable, so reads after a store fold.
 * ael_slot maps (array, index) to a numbered slot (NKV of them per function), creating it on request,
 * and refuses every array whose address escapes (SA_AESC), that has cleanup, volatile or atomic
 * elements, VLAs and wide types, and any index out of range.  The slots are valid only in straight-line
 * code: a label or loop header clears them (wk_label), so nothing is guessed across a join.
 */
#include "emitimpl.h"

/* the kv slot of a[CONST] (create: allocate one), or -1 */
VPRIV int ael_slot(ecx *e, uint32_t n, bool create)
{
	cc *c = e->c;
	uint32_t s, idx;
	const sym *y;
	const ctype *at;
	ast_qualified_type et;

	if (!is_aconst(c, n))
		return -1;
	s = N(N(N(n)->a)->a)->a, y = &c->syms[s];
	if (!is_local_var(c, s) || y->sc == AST_STORAGE_STATIC || (e->E->sflags[s] & SA_AESC) || sym_attrs(c, s)->cleanup)
		return -1;
	at = T(c, y->ty), et = at->base;
	if (at->kind != AST_TYPE_ARRAY || (at->flags & (AST_TYPE_FLAG_VLA | AST_TYPE_FLAG_INCOMPLETE)) || !is_integer(c, et) || is_wide(c, et) ||
	    (AST_QUALIFIED_TYPE_QUALIFIERS(et) & AST_QUALIFIER_VOLATILE) || (T(c, et)->flags & AST_TYPE_FLAG_ATOMIC) || (AST_QUALIFIED_TYPE_QUALIFIERS(y->ty) & AST_QUALIFIER_VOLATILE))
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
