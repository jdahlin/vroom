/* Switch dispatch policy shared by every target. No AST walk or allocation. */
#include "../emitimpl.h"

VPRIV int em_switch_strategy(uint32_t n, i128 lo, i128 hi, bool ranges, bool tables)
{
	/* Compare in unsigned 128 bits: even extreme signed/unsigned 64-bit
	 * endpoints cannot overflow. A table remains bounded to 65536 entries. */
	u128 span = (u128)hi - (u128)lo;
	if (tables && n >= 4 && !ranges && hi >= lo &&
	    span < (u128)4 * n + 8 && span < 65536)
		return EM_SWITCH_TABLE;
	return n >= 8 ? EM_SWITCH_TREE : EM_SWITCH_LINEAR;
}

static i128 em_switch_normalize(i128 v, unsigned bits, bool sg)
{
	uint64_t mask = bits == 64 ? UINT64_MAX : UINT32_MAX;
	uint64_t u = (uint64_t)v & mask;
	if (sg && (u & (UINT64_C(1) << (bits - 1))))
		return (i128)u - ((i128)1 << bits);
	return (i128)u;
}

static int em_switch_compare(const void *a, const void *b)
{
	const em_switch_case *x = a, *y = b;
	return (x->lo > y->lo) - (x->lo < y->lo);
}

VPRIV bool em_switch_prepare(em_switch_case *cases, uint32_t n, unsigned bits, bool sg)
{
	for (uint32_t i = 0; i < n; i++) {
		cases[i].lo = em_switch_normalize(cases[i].lo, bits, sg);
		cases[i].hi = em_switch_normalize(cases[i].hi, bits, sg);
		if (cases[i].lo > cases[i].hi)
			return false;
	}
	qsort(cases, n, sizeof *cases, em_switch_compare);
	for (uint32_t i = 1; i < n; i++)
		if (cases[i - 1].hi >= cases[i].lo)
			return false;
	return true;
}

VPRIV bool em_switch_bitset_plan(cc *c, uint32_t body, uint32_t nlabels,
				 unsigned bits, bool sg, em_switch_bitset_recipe *out)
{
	const node *b = &c->nodes[body];
	em_switch_bitset_recipe r = { 0 };
	i128 hi = 0;
	uint32_t cases = 0, defaults = 0;
	bool ended = false;

	/* This is a bounded inspection of the direct statement list, not another
	 * traversal. Count agreement with the existing collector rejects any case
	 * hidden in a nested statement. Allocation and sorting are unnecessary. */
	if ((bits != 32 && bits != 64) || nlabels < 4 || nlabels > 65 ||
	    b->op != N_BLOCK || b->c > 128)
		return false;
	for (uint32_t i = 0; i < b->c; i++) {
		uint32_t id = c->lists[b->b + i];
		const node *s = &c->nodes[id];

		if (s->op != N_CASE) {
			defaults += s->op == N_DEFAULT;
			if (cases) {
				/* A default in the empty label run shares its destination;
				 * membership then adds no useful dispatch. */
				if (!ended && s->op == N_DEFAULT)
					return false;
				ended = true;
			}
			continue;
		}
		if (ended || cases == 64)
			return false;
		i128 lo = em_switch_normalize(c->ipool[s->a], bits, sg);
		i128 top = em_switch_normalize(c->ipool[s->b], bits, sg);
		if (lo > top)
			return false;
		if (!cases) {
			r.lo = lo;
			hi = top;
		}
		i128 base = lo < r.lo ? lo : r.lo;
		if (top > hi)
			hi = top;
		if ((u128)hi - (u128)base >= 64)
			return false;
		if (base < r.lo)
			r.mask <<= (unsigned)(r.lo - base);
		r.lo = base;
		unsigned len = (unsigned)(top - lo) + 1;
		uint64_t run = (len == 64 ? UINT64_MAX : (UINT64_C(1) << len) - 1)
			       << (unsigned)(lo - base);
		if (r.mask & run)
			return false;
		r.mask |= run;
		r.node = id;
		cases++;
		/* The parser flattens a stacked run: only its last label has a body.
		 * A later case would therefore have a different destination. */
		ended = s->c != 0;
	}
	if (cases < 4 || cases + defaults != nlabels)
		return false;
	/* A zero base avoids subtraction when every case already fits a word. */
	if (r.lo >= 0 && hi < 64) {
		r.mask <<= (unsigned)r.lo;
		r.lo = 0;
	}
	r.span = (uint8_t)(hi - r.lo);
	if (r.mask == (r.span == 63 ? UINT64_MAX : (UINT64_C(1) << (r.span + 1)) - 1))
		return false;
	r.bits = r.span < 32 ? 32 : 64;
	*out = r;
	return true;
}
