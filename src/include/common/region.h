/*
 * Bump allocators over memory the caller reserved: vregion (by offset: the
 * assembler, the object writer) and varena (by pointer, 8-byte granules: the
 * preprocessor).  Neither maps anything; the reservations come from
 * port/job-regions.h.
 *
 * Under ASan (common/asan.h) the bytes past `used` are poisoned, a window of
 * VASAN_WINDOW at a time, and each allocation is unpoisoned as it is handed
 * out: a read or write past what was allocated is reported.  Set a region up
 * with vregion_init and move `used` back (a reset, a mark) with
 * vregion_truncate, so the bytes given back are poisoned again.
 */
#ifndef VCC_COMMON_REGION_H
#define VCC_COMMON_REGION_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/asan.h"
#include "common/attributes.h"
#include "common/fail.h"

/* a bump region over a reservation, by offset (the assembler, the object writer) */
typedef struct vregion {
    uint8_t *base;
    size_t used, cap;
#if VCC_ASAN
    size_t poisoned; /* [used, poisoned) is poisoned */
#endif
} vregion;

#if VCC_ASAN
/* keep at least half a window poisoned past end */
static inline void vregion_asan_extend(vregion *r, size_t end)
{
    size_t stop;

    if (end + VASAN_WINDOW / 2 <= r->poisoned)
        return;
    stop = r->cap;
    if (end + VASAN_WINDOW < stop)
        stop = end + VASAN_WINDOW;
    if (r->poisoned < end)
        r->poisoned = end;
    if (stop > r->poisoned)
        VASAN_POISON(r->base + r->poisoned, stop - r->poisoned);
    r->poisoned = stop;
}
#endif

/* an empty region over [base, base + cap) */
static inline void vregion_init(vregion *r, void *base, size_t cap)
{
    r->base = (uint8_t *)base;
    r->used = 0;
    r->cap = cap;
#if VCC_ASAN
    r->poisoned = 0;
    vregion_asan_extend(r, 0);
#endif
}

/* ASan: start poisoning a region whose base, used and cap were set by hand */
static inline void vregion_asan_start(vregion *r)
{
#if VCC_ASAN
    r->poisoned = r->used;
    vregion_asan_extend(r, r->used);
#else
    (void)r;
#endif
}

/* back to used bytes (used <= r->used): what was allocated past it is gone */
#if VCC_ASAN
static inline void vregion_truncate(vregion *r, size_t used)
{
    if (r->poisoned > used)
        VASAN_POISON(r->base + used, r->poisoned - used);
    else
        r->poisoned = used;
    r->used = used;
}
#else
/* the plain store: an inline function changes the code clang makes around it */
#define vregion_truncate(r, u) ((void)((r)->used = (u)))
#endif

/* the whole reservation addressable again, before it is unmapped or handed back */
#if VCC_ASAN
static inline void vregion_unpoison(vregion *r)
{
    if (r->poisoned)
        VASAN_UNPOISON(r->base, r->poisoned);
    r->poisoned = 0;
}
#else
#define vregion_unpoison(r) ((void)(r))
#endif

/* the n bytes at offset u (aligned, u + n <= cap checked by the caller) */
static inline VUSE
    VRETNONNULL VNONNULL(1) void *vregion_take(vregion *r, size_t u, size_t n)
{
    r->used = u + n;
#if VCC_ASAN
    VASAN_UNPOISON(r->base + u, n);
    vregion_asan_extend(r, u + n);
#endif
    return r->base + u;
}

/* n bytes aligned to al (a power of two); fails the job with msg when full */
static inline VUSE VRETNONNULL VNONNULL(1) void *vregion_push(vregion *r,
                                                              size_t n,
                                                              size_t al,
                                                              const char *msg)
{
    size_t u = (r->used + al - 1) & ~(al - 1);

    if (u + n > r->cap)
        vjob_fail(msg);
    return vregion_take(r, u, n);
}

/* a vregion used in 8-byte granules, as the preprocessor does (reset by vregion_truncate) */
typedef vregion varena;

static inline VUSE VRETNONNULL VNONNULL(1) void *varena_alloc(varena *a,
                                                              size_t n)
{
    return vregion_push(a, (n + 7) & ~(size_t)7, 1, "preprocessor arena full");
}

#endif
