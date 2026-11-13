/*
 * Bump allocators over memory the caller reserved: vregion (by offset: the
 * assembler, the object writer) and varena (by pointer, 8-byte granules: the
 * preprocessor).  Neither maps anything; the reservations come from
 * port/job-regions.h.
 */
#ifndef VCC_REGION_H
#define VCC_REGION_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/attr.h"

/* a bump region over a reservation, by offset (the assembler, the object writer) */
typedef struct vregion { uint8_t *base; size_t used, cap; } vregion;

/* n bytes aligned to al (a power of two); prints msg and aborts when full */
static inline VUSE VRETNONNULL VNONNULL(1) void *vregion_push(vregion *r, size_t n, size_t al, const char *msg)
{
	size_t u = (r->used + al - 1) & ~(al - 1);

	if (u + n > r->cap) {
		fprintf(stderr, "%s\n", msg);
		abort();
	}
	r->used = u + n;
	return r->base + u;
}

/* a bump arena by pointers, 8-byte granules (the preprocessor) */
typedef struct varena {
	char *base, *p, *end;
} varena;

static inline VUSE VRETNONNULL VNONNULL(1) void *varena_alloc(varena *a, size_t n)
{
	char *r = a->p;

	n = (n + 7) & ~(size_t)7;
	if (__builtin_expect((size_t)(a->end - r) < n, 0)) {
		fprintf(stderr, "vcc: preprocessor arena full\n");
		abort();
	}
	a->p = r + n;
	return r;
}

#endif
