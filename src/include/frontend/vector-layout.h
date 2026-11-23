#ifndef VCC_FRONTEND_VECTOR_LAYOUT_H
#define VCC_FRONTEND_VECTOR_LAYOUT_H
/*
 * vector-layout.h: the layout of GNU's vector_size types, shared by the C
 * front end (cc_type_vector) and the C++ front end's analysis, so that both
 * give a vector the same size and alignment.  A vector of `size` bytes is
 * `size` bytes; its alignment is its size, except that arm64 caps it at 16
 * (x86-64 and wasm use the full size).
 */
#include <stdbool.h>
#include <stdint.h>

static inline uint32_t vector_alignment(uint32_t size, bool uncapped)
{
    if (size > 16 && !uncapped)
        return 16;
    return size;
}

/* clang's rule for vector_size(size) of an element of element_size bytes:
   the element is an integer or floating-point type (not bool, the caller
   checks), the size a multiple of the element's */
static inline bool vector_size_fits(uint64_t size, uint64_t element_size)
{
    return element_size && element_size <= size && size % element_size == 0;
}

#endif
