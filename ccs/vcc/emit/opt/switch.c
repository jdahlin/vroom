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
