/*
 * Tail-merged string tables, as LLVM's StringTableBuilder lays them out for
 * ELF (.strtab) and COFF (the symbol table's string table): the strings
 * sorted by their characters from the end, greater first, so that a string
 * that is a suffix of the one before it shares that one's bytes.
 *
 *   vstring_table_layout(v, n, start)
 *       sorts v and stores each string's offset in *v[i].out, counting from
 *       start (ELF: 1, after the empty name; COFF: 4, after the size field);
 *       returns the table's size from 0 (start included)
 *   vstring_table_fill(t, v, n, bias)
 *       copies each string and its 0 to t + *out - bias
 *
 * Strings need no terminator (s, n); equal strings share one offset.
 */
#ifndef VCC_COMMON_STRING_TABLE_H
#define VCC_COMMON_STRING_TABLE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct vstring_ref {
	const char *s;
	uint32_t n;
	uint32_t *out;		/* where its offset goes */
} vstring_ref;

static inline int vstring_ref_tail_compare(const void *pa, const void *pb)
{
	const vstring_ref *a = pa, *b = pb;

	for (uint32_t i = 0;; i++) {
		int ca = i < a->n ? (unsigned char)a->s[a->n - 1 - i] : -1;
		int cb = i < b->n ? (unsigned char)b->s[b->n - 1 - i] : -1;

		if (ca != cb)
			return ca > cb ? -1 : 1;	/* descending; a suffix after the longer string */
		if (ca < 0)
			return 0;
	}
}

static inline uint32_t vstring_table_layout(vstring_ref *v, uint32_t n, uint32_t start)
{
	uint32_t size = start, prevoff = 0, prevn = 0;
	const char *prev = NULL;

	qsort(v, n, sizeof *v, vstring_ref_tail_compare);
	for (uint32_t i = 0; i < n; i++) {
		if (prev && prevn >= v[i].n && !memcmp(prev + prevn - v[i].n, v[i].s, v[i].n)) {
			*v[i].out = prevoff + prevn - v[i].n;
			continue;
		}
		*v[i].out = size;
		prevoff = size, prev = v[i].s, prevn = v[i].n;
		size += v[i].n + 1;
	}
	return size;
}

static inline void vstring_table_fill(uint8_t *t, const vstring_ref *v, uint32_t n, uint32_t bias)
{
	for (uint32_t i = 0; i < n; i++) {
		memcpy(t + *v[i].out - bias, v[i].s, v[i].n);
		t[*v[i].out - bias + v[i].n] = 0;
	}
}

#endif
