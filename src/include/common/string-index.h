/*
 * A table from a string to the index of an entry, over entries the caller
 * keeps in its own array (symbols, sections, names): the table stores only
 * indexes, so an entry is never copied or moved by it.  Open addressing with
 * linear probing on vhash_string (hash.h); the slots hold index + 1, so zero
 * is empty.  No deletion.
 *
 * The caller says where an entry's name is with a function
 *   const char *name(const void *entries, uint32_t index, uint32_t *length)
 * (entries is what was passed to the call, usually the array).
 *
 *   vstring_index_find(t, entries, name, s, n)   the index of the entry named s[0..n),
 *                                                or VSTRING_INDEX_NONE
 *   vstring_index_add(t, entries, name, index)   enter entries[index] (its name is read
 *                                                through `name`); the caller has checked
 *                                                that no entry has that name.  Grows at
 *                                                half full, so a probe is short; out of
 *                                                memory fails the job (vout_of_memory)
 *   vstring_index_free(t)                        the slots; the table is empty again
 *
 * A zero table is empty and ready.  Do not add under a scope that holds a
 * VAUTO* or VLOCK resource (it can fail the job).
 */
#ifndef VCC_COMMON_STRING_INDEX_H
#define VCC_COMMON_STRING_INDEX_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common/fail.h"
#include "common/hash.h"

#define VSTRING_INDEX_NONE UINT32_MAX

typedef struct vstring_index {
	uint32_t *slot;		/* index + 1, or 0 */
	uint32_t nslot;		/* a power of two, or 0 */
	uint32_t count;
} vstring_index;

typedef const char *(*vstring_index_name_fn)(const void *entries, uint32_t index, uint32_t *length);

static inline uint32_t vstring_index_find(const vstring_index *t, const void *entries, vstring_index_name_fn name,
					  const char *s, uint32_t n)
{
	uint32_t mask, i;

	if (!t->nslot)
		return VSTRING_INDEX_NONE;
	mask = t->nslot - 1;
	for (i = vhash_string(s, n) & mask; t->slot[i]; i = (i + 1) & mask) {
		uint32_t len, index = t->slot[i] - 1;
		const char *e = name(entries, index, &len);

		if (len == n && !memcmp(e, s, n))
			return index;
	}
	return VSTRING_INDEX_NONE;
}

static inline void vstring_index_insert_(uint32_t *slot, uint32_t nslot, uint32_t hash, uint32_t index)
{
	uint32_t mask = nslot - 1, i = hash & mask;

	while (slot[i])
		i = (i + 1) & mask;
	slot[i] = index + 1;
}

static inline void vstring_index_add(vstring_index *t, const void *entries, vstring_index_name_fn name, uint32_t index)
{
	uint32_t len;
	const char *s;

	if ((uint64_t)(t->count + 1) * 2 > t->nslot) {
		uint32_t nn = t->nslot ? t->nslot * 2 : 16;
		uint32_t *ns;

		if (!nn)
			vout_of_memory();
		ns = calloc(nn, sizeof *ns);
		if (!ns)
			vout_of_memory();
		for (uint32_t i = 0; i < t->nslot; i++) {
			if (t->slot[i]) {
				const char *old = name(entries, t->slot[i] - 1, &len);

				vstring_index_insert_(ns, nn, vhash_string(old, len), t->slot[i] - 1);
			}
		}
		free(t->slot);
		t->slot = ns;
		t->nslot = nn;
	}
	s = name(entries, index, &len);
	vstring_index_insert_(t->slot, t->nslot, vhash_string(s, len), index);
	t->count++;
}

static inline void vstring_index_free(vstring_index *t)
{
	free(t->slot);
	*t = (vstring_index){ 0 };
}

#endif
