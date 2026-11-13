/* vcc front end: AST node constructors */
#include "ccimpl.h"

/* ---------------- nodes ---------------- */

VPRIV uint32_t mk(cc *c, int op, qty ty, uint32_t loc)
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
VPRIV pp_span rng_np(cc *c, uint32_t n)
{
	while (n && !c->span[n].e && N(n)->op == N_CAST && N(n)->a)
		n = N(n)->a;
	if (!n)
		return (pp_span){ 0, 0 };
	if (!c->span[n].e)
		return (pp_span){ N(n)->loc, pp_tok_end(c->p, N(n)->loc) };
	return (pp_span){ c->span[n].b, pp_tok_end(c->p, c->span[n].e) };
}

/* ... with its outermost parentheses (clang's ParenExpr) */
VPRIV pp_span rng(cc *c, uint32_t n)
{
	pp_span r;

	while (n && !c->span[n].e && N(n)->op == N_CAST && N(n)->a)
		n = N(n)->a;
	r = rng_np(c, n);
	if (n && (c->paren[n].l || c->paren[n].r)) {
		uint32_t e = (c->span[n].e ? c->span[n].e : N(n)->loc) + c->paren[n].r;

		r.b -= c->paren[n].l, r.e = pp_tok_end(c->p, e);
	}
	return r;
}

VPRIV uint32_t mk2(cc *c, int op, qty ty, uint32_t loc, uint32_t a, uint32_t b)
{
	uint32_t i = mk(c, op, ty, loc);

	c->nodes[i].a = a;
	c->nodes[i].b = b;
	return i;
}

VPRIV void vinit(vec *v) { v->v = v->inl, v->n = 0, v->cap = 16; }

VPRIV void vpush(cc *c, vec *v, uint32_t x)
{
	if (unlikely(v->n == v->cap)) {
		uint32_t *nv = halloc(c, v->cap * 2 * sizeof(uint32_t));

		memcpy(nv, v->v, v->n * sizeof(uint32_t));
		v->v = nv;
		v->cap *= 2;
	}
	v->v[v->n++] = x;
}

VPRIV uint32_t vfreeze(cc *c, const vec *v)
{
	uint32_t at = c->nlist, *d = &c->lists[at];

	/* 16-byte blocks, inline: v->v has room for n rounded up to 16 (inl[16], doubling),
	   and c->lists past nlist is free (reserved) */
	for (uint32_t i = 0; i < v->n; i += 4)
		memcpy(d + i, v->v + i, 16);
	c->nlist += v->n;
	return at;
}

VPRIV uint32_t mk_int(cc *c, i128 v, qty ty, uint32_t loc)
{
	if (is_bigint(c, ty)) return big_int_node(c, v, ty, loc);
	uint32_t n = mk(c, N_NUM, ty, loc);

	c->ipool[c->nipool] = v;
	c->nodes[n].a = c->nipool++;
	return n;
}

/* stands in for an expression already diagnosed: operators on it say nothing more
   (clang's invalid expressions); an int 0 lvalue, so a constant and assignable */
VPRIV uint32_t err_node(cc *c, uint32_t loc)
{
	uint32_t n = mk_int(c, 0, prim(TY_INT), loc);

	c->nodes[n].flags |= NF_ERR | NF_LVALUE;
	delayed_bug(c, loc, "an error node without an error");
	return n;
}

VPRIV uint32_t mk_flt(cc *c, double v, qty ty, uint32_t loc)
{
	uint32_t n = mk(c, N_FNUM, ty, loc);

	c->fpool[c->nfpool] = v;
	c->nodes[n].a = c->nfpool++;
	if (is_q(c, ty))
		fpool_q(c, q_of_d(v));
	return n;
}

/* a 16-byte long double constant from its bits */
VPRIV uint32_t mk_fltq(cc *c, u128 q, qty ty, uint32_t loc)
{
	uint32_t n = mk(c, N_FNUM, ty, loc);

	c->fpool[c->nfpool] = q_to_d(q);
	c->nodes[n].a = c->nfpool++;
	fpool_q(c, q);
	return n;
}

VPRIV uint32_t mk_bin(cc *c, int op, qty ty, uint32_t a, uint32_t b, uint32_t loc)
{
	uint32_t r = mk2(c, N_BINARY, ty, loc, a, b);

	N(r)->aux = (uint16_t)op;
	return r;
}
