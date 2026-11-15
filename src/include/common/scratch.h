#ifndef VCC_COMMON_SCRATCH_H
#define VCC_COMMON_SCRATCH_H
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
    *s = (vscratch){0};
}

#endif
