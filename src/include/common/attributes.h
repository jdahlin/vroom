/*
 * Attributes for our own declarations, so that the compiler (clang, and vcc
 * itself) checks what the comments used to say:
 *
 *   VUSE         the result must be used (an error status, or a pointer that
 *                is the whole point of the call)
 *   VNONNULL(n)  argument n (1-based) is not NULL; VNONNULL_ALL: every pointer argument
 *   VRETNONNULL  never returns NULL (the function aborts instead)
 *   VUNCONST(T, p)  p as T without its const: argv for exec and posix_spawn,
 *                memory freed or unmapped through a const pointer, a read-only
 *                view handed to an API that takes a mutable one.  The one
 *                place -Wcast-qual allows it; grep for it in review.
 *
 * counted_by is not used: it is for flexible array members, and the code has
 * none (buffers are a pointer, a length and a capacity).
 */
#ifndef VCC_COMMON_ATTRIBUTES_H
#define VCC_COMMON_ATTRIBUTES_H

#define VUSE __attribute__((warn_unused_result))
#define VNONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#define VNONNULL_ALL __attribute__((nonnull))
#define VRETNONNULL __attribute__((returns_nonnull))
#include <stdint.h>
#define VUNCONST(T, p) ((T)(uintptr_t)(const volatile void *)(p))

#endif
