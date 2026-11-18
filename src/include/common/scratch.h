/*
 * scratch.h: vscratch, one reusable heap block a pass sizes up front and
 * cuts into views, kept by its owner from one run to the next so that the
 * pass allocates only when a run needs more than any before.
 */
#ifndef VCC_COMMON_SCRATCH_H
#define VCC_COMMON_SCRATCH_H
#include "common/literal.h"
#include <stdlib.h>

/* Reusable heap-backed workspace for passes that can size all tables up front.
 * Zero-initialize the owner. Each get starts a new lifetime for every view,
 * returns uninitialized storage, and leaves the owner intact on failure. */
typedef struct vscratch {
    void *p;
    size_t cap;
} vscratch;

static inline void *vscratch_get(vscratch *s, size_t bytes)
{
    if (bytes > s->cap) {
        void *p = realloc(s->p, bytes);

        if (!p)
            return NULL;
        s->p = p;
        s->cap = bytes;
    }
    return s->p;
}

static inline void vscratch_destroy(vscratch *s)
{
    free(s->p);
    *s = VCC_LITERAL(vscratch, 0);
}

#endif
