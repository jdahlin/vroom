/*
 * Bump allocators over memory the caller reserved: vregion (by offset: the
 * assembler, the object writer) and varena (by pointer, 8-byte granules: the
 * preprocessor).  Neither maps anything; the reservations come from
 * port/job-regions.h.
 */
#ifndef VCC_COMMON_REGION_H
#define VCC_COMMON_REGION_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/attributes.h"
#include "common/fail.h"

/* a bump region over a reservation, by offset (the assembler, the object writer) */
typedef struct vregion { uint8_t *base; size_t used, cap; } vregion;

/* n bytes aligned to al (a power of two); fails the job with msg when full */
static inline VUSE VRETNONNULL VNONNULL(1) void *vregion_push(vregion *r, size_t n, size_t al, const char *msg)
{
	size_t u = (r->used + al - 1) & ~(al - 1);

	if (u + n > r->cap)
		vjob_fail(msg);
	r->used = u + n;
	return r->base + u;
}

/* a vregion used in 8-byte granules, as the preprocessor does (base, used, cap: reset by used = 0) */
typedef vregion varena;

static inline VUSE VRETNONNULL VNONNULL(1) void *varena_alloc(varena *a, size_t n)
{
	return vregion_push(a, (n + 7) & ~(size_t)7, 1, "preprocessor arena full");
}

#endif
