/* vcc front end: AST node constructors */
#include "cc-internal.h"

/* ---------------- nodes ---------------- */

CC_PRIVATE uint32_t cc_node_new(cc *c, int op, qty ty, uint32_t loc)
{
	uint32_t i = c->nnode++;
	node *n = &c->nodes[i];

	memset(n, 0, sizeof(*n));
	n->op = (uint8_t)op;
	n->ty = ty;
	n->loc = loc;
	return i;
}

/* the character range of n for a diagnostic (implicit conversions: their operand's) */
CC_PRIVATE diag_span cc_node_range_bare(cc *c, uint32_t n)
{
	while (n && !c->span[n].e && AST_NODE(n)->op == N_CAST && AST_NODE(n)->a)
		n = AST_NODE(n)->a;
	if (!n)
		return (diag_span){ 0, 0 };
	if (!c->span[n].e)
		return (diag_span){ AST_NODE(n)->loc, pp_token_end_find(c->p, AST_NODE(n)->loc) };
	return (diag_span){ c->span[n].b, pp_token_end_find(c->p, c->span[n].e) };
}

/* ... with its outermost parentheses (clang's ParenExpr) */
CC_PRIVATE diag_span cc_node_range(cc *c, uint32_t n)
{
	diag_span r;

	while (n && !c->span[n].e && AST_NODE(n)->op == N_CAST && AST_NODE(n)->a)
		n = AST_NODE(n)->a;
	r = cc_node_range_bare(c, n);
	if (n && (c->paren[n].l || c->paren[n].r)) {
		uint32_t e = (c->span[n].e ? c->span[n].e : AST_NODE(n)->loc) + c->paren[n].r;

		r.b -= c->paren[n].l, r.e = pp_token_end_find(c->p, e);
	}
	return r;
}

CC_PRIVATE uint32_t cc_node_new_operands(cc *c, int op, qty ty, uint32_t loc, uint32_t a, uint32_t b)
{
	uint32_t i = cc_node_new(c, op, ty, loc);

	c->nodes[i].a = a;
	c->nodes[i].b = b;
	return i;
}

CC_PRIVATE void cc_id_list_init(cc_id_list *v) { v->v = v->inl, v->n = 0, v->cap = 16; }

CC_PRIVATE void cc_id_list_append(cc *c, cc_id_list *v, uint32_t x)
{
	if (unlikely(v->n == v->cap)) {
		uint32_t *nv = cc_heap_allocate(c, v->cap * 2 * sizeof(uint32_t));

		memcpy(nv, v->v, v->n * sizeof(uint32_t));
		v->v = nv;
		v->cap *= 2;
	}
	v->v[v->n++] = x;
}

CC_PRIVATE uint32_t cc_id_list_freeze(cc *c, const cc_id_list *v)
{
	uint32_t at = c->nlist, *d = &c->lists[at];

	/* 16-byte blocks, inline: v->v has room for n rounded up to 16 (inl[16], doubling),
	   and c->lists past nlist is free (reserved) */
	for (uint32_t i = 0; i < v->n; i += 4)
		memcpy(d + i, v->v + i, 16);
	c->nlist += v->n;
	return at;
}

CC_PRIVATE uint32_t cc_node_integer_new(cc *c, i128 v, qty ty, uint32_t loc)
{
	if (cc_type_is_bitint(c, ty)) return cc_bitint_node_new_integer(c, v, ty, loc);
	uint32_t n = cc_node_new(c, N_NUM, ty, loc);

	c->ipool[c->nipool] = v;
	c->nodes[n].a = c->nipool++;
	return n;
}

/* stands in for an expression already diagnosed: operators on it say nothing more
   (clang's invalid expressions); an int 0 lvalue, so a constant and assignable */
CC_PRIVATE uint32_t cc_node_error_new(cc *c, uint32_t loc)
{
	uint32_t n = cc_node_integer_new(c, 0, cc_type_basic(TY_INT), loc);

	c->nodes[n].flags |= NF_ERR | NF_LVALUE;
	cc_report_delayed_bug(c, loc, "an error node without an error");
	return n;
}

CC_PRIVATE uint32_t cc_node_float_new(cc *c, double v, qty ty, uint32_t loc)
{
	uint32_t n = cc_node_new(c, N_FNUM, ty, loc);

	c->fpool[c->nfpool] = v;
	c->nodes[n].a = c->nfpool++;
	if (cc_type_is_float128(c, ty))
		cc_float_pool_append_float128(c, vsoftfloat_of_double(v));
	return n;
}

/* a 16-byte long double constant from its bits */
CC_PRIVATE uint32_t cc_node_float128_new(cc *c, u128 q, qty ty, uint32_t loc)
{
	uint32_t n = cc_node_new(c, N_FNUM, ty, loc);

	c->fpool[c->nfpool] = vsoftfloat_to_double(q);
	c->nodes[n].a = c->nfpool++;
	cc_float_pool_append_float128(c, q);
	return n;
}

CC_PRIVATE uint32_t cc_node_binary_new(cc *c, int op, qty ty, uint32_t a, uint32_t b, uint32_t loc)
{
	uint32_t r = cc_node_new_operands(c, N_BINARY, ty, loc, a, b);

	AST_NODE(r)->aux = (uint16_t)op;
	return r;
}
