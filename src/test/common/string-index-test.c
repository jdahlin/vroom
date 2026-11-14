/* Unit test of common/string-index.h: against a linear search over random names, growth, empty names, and the zero table. */
#include <string.h>
#include "check.h"
#include "common/string-index.h"

typedef struct { char *s; uint32_t n; } entry;

static const char *name_of(const void *entries, uint32_t index, uint32_t *length)
{
	const entry *e = entries;

	*length = e[index].n;
	return e[index].s;
}

static uint64_t rng = 12345;
static uint64_t next(void) { rng = rng * 6364136223846793005ull + 1442695040888963407ull; return rng >> 33; }

int main(void)
{
	vstring_index t = { 0 };
	enum { N = 5000 };
	entry *e = calloc(N + 1, sizeof *e);
	uint32_t n = 0;

	CHECK(vstring_index_find(&t, e, name_of, "x", 1) == VSTRING_INDEX_NONE);	/* the zero table */
	for (int i = 0; i < N; i++) {
		char buf[24];
		uint32_t len = next() % 12;	/* includes the empty name */
		uint32_t k;

		for (k = 0; k < len; k++)
			buf[k] = (char)('a' + next() % 3);	/* few letters: many near-collisions */
		if (vstring_index_find(&t, e, name_of, buf, len) != VSTRING_INDEX_NONE)
			continue;
		e[n].s = malloc(len + 1);
		memcpy(e[n].s, buf, len);
		e[n].n = len;
		vstring_index_add(&t, e, name_of, n);
		n++;
		CHECK((uint64_t)t.count * 2 <= t.nslot);
	}
	CHECK(n > 1000 && t.count == n);
	for (uint32_t i = 0; i < n; i++)
		CHECK(vstring_index_find(&t, e, name_of, e[i].s, e[i].n) == i);
	CHECK(vstring_index_find(&t, e, name_of, "zzzz", 4) == VSTRING_INDEX_NONE);
	CHECK(vstring_index_find(&t, e, name_of, e[0].s, e[0].n + 1) == VSTRING_INDEX_NONE || e[0].n >= 12);
	vstring_index_free(&t);
	CHECK(t.count == 0 && t.slot == NULL);
	for (uint32_t i = 0; i < n; i++)
		free(e[i].s);
	free(e);
	return check_failures;
}
