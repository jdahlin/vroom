/*
 * Growable arrays, in two forms.
 *
 * Over three lvalues: the array pointer `v`, its count `n` and its capacity
 * `cap` (unsigned integers of any width, usually fields of one struct):
 *
 *   VVECTOR_NEED(v, n, cap, k)       room for k more elements
 *   VVECTOR_APPEND(v, n, cap, x)     v[n++] = x, growing first if needed
 *
 * The heap forms grow with realloc: the capacity starts at 8 and doubles,
 * and a request that does not fit the type of `cap` (or in memory) fails the
 * job through vout_of_memory (common/fail.h), so they never return on failure.
 * They must not run in a scope that holds a VAUTO* or VLOCK resource
 * (scoped-cleanup.h).  `v` is NULL while `cap` is 0.
 *
 *   VVECTOR_NEED_IN(alloc, v, n, cap, k), VVECTOR_APPEND_IN(alloc, v, n, cap, x)
 *     the same over memory from alloc(bytes) (a region or per-thread arena:
 *     it must not return NULL); the old array is copied and never freed.
 *
 * VVECTOR(T) is the usual struct { T *v; uint32_t n, cap; } to declare a
 * vector with these macros: VVECTOR_APPEND(a.v, a.n, a.cap, x).
 *
 * VVEC and VVEC_PUSH are the older arena-only form the linker uses; its pass
 * moves to the macros above.  Arguments are evaluated more than once: pass
 * lvalues without side effects.
 */
#ifndef VCC_COMMON_VECTOR_H
#define VCC_COMMON_VECTOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common/fail.h"

/* the next capacity (8, then doubling) that holds `need` elements of `size` bytes, or 0 when the byte count overflows */
static inline size_t vvector_capacity(size_t cap, size_t need, size_t size)
{
	size_t bytes;

	if (cap < 8)
		cap = 8;
	while (cap < need) {
		if (cap > SIZE_MAX / 2)
			return 0;
		cap *= 2;
	}
	if (__builtin_mul_overflow(cap, size, &bytes))
		return 0;
	return cap;
}

/* does `cap` (any unsigned type: the macro passes its own width as `max`) hold the capacity */
#define VVECTOR_CAPACITY_(cap, newcap) ((size_t)(__typeof__(cap))(newcap) == (newcap))

#define VVECTOR_NEED(v, n, cap, k) do { \
	size_t vv_need_ = (size_t)(n) + (size_t)(k); \
	if (vv_need_ < (size_t)(n)) \
		vout_of_memory(); \
	if (vv_need_ > (size_t)(cap)) { \
		size_t vv_cap_ = vvector_capacity((size_t)(cap), vv_need_, sizeof(*(v))); \
		void *vv_p_; \
		if (!vv_cap_ || !VVECTOR_CAPACITY_(cap, vv_cap_)) \
			vout_of_memory(); \
		vv_p_ = realloc((v), vv_cap_ * sizeof(*(v))); \
		if (!vv_p_) \
			vout_of_memory(); \
		(v) = vv_p_; \
		(cap) = (__typeof__(cap))vv_cap_; \
	} \
} while (0)

#define VVECTOR_APPEND(v, n, cap, x) do { \
	VVECTOR_NEED(v, n, cap, 1); \
	(v)[(n)++] = (x); \
} while (0)

#define VVECTOR_NEED_IN(alloc, v, n, cap, k) do { \
	size_t vv_need_ = (size_t)(n) + (size_t)(k); \
	if (vv_need_ < (size_t)(n)) \
		vout_of_memory(); \
	if (vv_need_ > (size_t)(cap)) { \
		size_t vv_cap_ = vvector_capacity((size_t)(cap), vv_need_, sizeof(*(v))); \
		void *vv_p_; \
		if (!vv_cap_ || !VVECTOR_CAPACITY_(cap, vv_cap_)) \
			vout_of_memory(); \
		vv_p_ = alloc(vv_cap_ * sizeof(*(v))); \
		if ((n)) \
			memcpy(vv_p_, (v), (size_t)(n) * sizeof(*(v))); \
		(v) = vv_p_; \
		(cap) = (__typeof__(cap))vv_cap_; \
	} \
} while (0)

#define VVECTOR_APPEND_IN(alloc, v, n, cap, x) do { \
	VVECTOR_NEED_IN(alloc, v, n, cap, 1); \
	(v)[(n)++] = (x); \
} while (0)

#define VVECTOR(T) struct { T *v; uint32_t n, cap; }

/* the arena-only form of the linker (see above) */
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
