/* If-conversion with an arm that reads the branch's condition again: in
   `cap ? cap * 2 : 256` the arm reloads cap, the builder reuses the
   condition's value, and the arm read the condition itself.  The pass moved
   the arm in front of the condition, and VIR rejected the function ("a value
   used before its definition"; -fvir-report showed the fallback).  Each
   growth step is checked against the arithmetic done by hand. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { uint64_t a, b; uint32_t f; } blk;
typedef struct { blk *v; uint32_t n, cap; } vec;

__attribute__((noinline)) static uint32_t push(vec *c)
{
	uint32_t b = c->n;

	if ((uint64_t)c->n + 1 > c->cap) {
		size_t nc = c->cap ? (size_t)c->cap * 2 : 256;

		while (nc < (uint64_t)c->n + 1)
			nc *= 2;
		void *q = realloc(c->v, nc * sizeof *c->v);

		if (!q)
			abort();
		c->v = q;
		c->cap = (uint32_t)nc;
	}
	c->n++;
	c->v[b] = (blk){ 0 };
	c->v[b].f = 0xffffffff;
	return b;
}

__attribute__((noinline)) static unsigned next_cap(unsigned cap, unsigned need)
{
	unsigned nc = cap ? cap * 2 : 256;

	while (nc < need)
		nc *= 2;
	return nc;
}

int main(void)
{
	vec c = { 0 };
	int bad = 0;

	for (uint32_t i = 0; i < 1000; i++) {
		uint32_t want = i < 256 ? 256 : i < 512 ? 512 : 1024;

		bad |= push(&c) != i || c.cap != want || c.v[i].f != 0xffffffff;
	}
	bad |= next_cap(0, 1) != 256 || next_cap(0, 300) != 512 || next_cap(3, 1) != 6 || next_cap(3, 13) != 24;
	printf("%s n=%u cap=%u\n", bad ? "FAIL" : "ok", c.n, c.cap);
	free(c.v);
	return bad;
}
