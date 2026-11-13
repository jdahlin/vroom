/*
 * Checked arithmetic for sizes and offsets that come out of an input file
 * (objects, archives, wasm modules).  A bounds check written as
 * `off + n <= size` is wrong when the addition wraps; these say what they
 * mean through the compiler's overflow builtins.
 *
 *   vchecked_range(off, n, size)   [off, off + n) lies inside [0, size)
 *   vchecked_add(a, b, &r)         r = a + b; false when it does not fit
 *   vchecked_multiply(a, b, &r)         r = a * b; false when it does not fit
 */
#ifndef VCC_COMMON_CHECKED_ARITHMETIC_H
#define VCC_COMMON_CHECKED_ARITHMETIC_H

#include <stdbool.h>
#include <stdint.h>

static inline bool vchecked_range(uint64_t off, uint64_t n, uint64_t size)
{
	uint64_t end;

	return !__builtin_add_overflow(off, n, &end) && end <= size;
}

/* the sums and products: true when the result is exact */
#define vchecked_add(a, b, r) (!__builtin_add_overflow((a), (b), (r)))
#define vchecked_multiply(a, b, r) (!__builtin_mul_overflow((a), (b), (r)))

#endif
