/*
 * Growable arrays over a caller's allocator: VVEC(T) is { T *v; uint32_t n,
 * cap; }, VVEC_PUSH(a, x, alloc) appends x, doubling (from 8) into memory
 * from alloc(bytes); the old array is not freed (arena allocators).  From
 * the linker, where alloc is its per-thread arena.
 */
#ifndef VCC_COMMON_VECTOR_H
#define VCC_COMMON_VECTOR_H

#include <stdint.h>
#include <string.h>

#define VVEC(T) struct { T *v; uint32_t n, cap; }
#define VVEC_PUSH(a, x, alloc) do { \
	if ((a).n == (a).cap) { \
		uint32_t nc = (a).cap ? (a).cap * 2 : 8; \
		void *nv = alloc(nc * sizeof(*(a).v)); \
		if ((a).n) memcpy(nv, (a).v, (a).n * sizeof(*(a).v)); \
		(a).v = nv; (a).cap = nc; \
	} \
	(a).v[(a).n++] = (x); \
} while (0)

#endif
