/*
 * known-values.c: the walker's slots for constant array elements: `a[K]` of a local integer array
 * used only with constant indices (codegen_array_element_slot).
 *
 * The walker tracks the value of such an element as it does a variable, so reads after a store fold.
 * codegen_array_element_slot maps (array, index) to a numbered slot (CODEGEN_KNOWN_VALUE_MAX of them per function), creating it on request,
 * and refuses every array whose address escapes (CODEGEN_SYMBOL_ARRAY_ESCAPES), that has cleanup, volatile or atomic
 * elements, VLAs and wide types, and any index out of range.  The slots are valid only in straight-line
 * code: a label or loop header clears them (walk_label_place), so nothing is guessed across a join.
 */
#include "codegen-internal.h"

/* the kv slot of a[CONST] (create: allocate one), or -1 */
VPRIV int codegen_array_element_slot(codegen_context *e, uint32_t n, bool create)
{
	cc *c = e->c;
	uint32_t s, idx;
	const sym *y;
	const ctype *at;
	ast_qualified_type et;

	if (!codegen_expression_is_constant_index(c, n))
		return -1;
	s = N(N(N(n)->a)->a)->a, y = &c->syms[s];
	if (!codegen_symbol_is_local_variable(c, s) || y->sc == AST_STORAGE_STATIC || (e->E->sflags[s] & CODEGEN_SYMBOL_ARRAY_ESCAPES) || codegen_symbol_attributes(c, s)->cleanup)
		return -1;
	at = T(c, y->ty), et = at->base;
	if (at->kind != AST_TYPE_ARRAY || (at->flags & (AST_TYPE_FLAG_VLA | AST_TYPE_FLAG_INCOMPLETE)) || !codegen_type_is_integer(c, et) || codegen_type_is_wide(c, et) ||
	    (AST_QUALIFIED_TYPE_QUALIFIERS(et) & AST_QUALIFIER_VOLATILE) || (T(c, et)->flags & AST_TYPE_FLAG_ATOMIC) || (AST_QUALIFIED_TYPE_QUALIFIERS(y->ty) & AST_QUALIFIER_VOLATILE))
		return -1;
	idx = codegen_expression_constant_index(c, n);
	if (idx >= at->n)
		return -1;
	for (int i = 0; i < e->nael; i++)
		if (e->ael[i].s == s && e->ael[i].idx == idx)
			return e->ael[i].kv;
	if (!create || e->nkv == CODEGEN_KNOWN_VALUE_MAX || e->nael == CODEGEN_KNOWN_VALUE_MAX)
		return -1;
	e->ael[e->nael].s = s, e->ael[e->nael].idx = idx, e->ael[e->nael].kv = e->nkv;
	e->nael++;
	e->kvalid &= (uint16_t)~(1u << e->nkv);
	return e->nkv++;
}
