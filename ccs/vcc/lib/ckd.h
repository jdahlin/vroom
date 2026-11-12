/*
 * Checked arithmetic for sizes and offsets that come out of an input file
 * (objects, archives, wasm modules).  A bounds check written as
 * `off + n <= size` is wrong when the addition wraps; these say what they
 * mean through the compiler's overflow builtins.
 *
 *   vckd_range(off, n, size)   [off, off + n) lies inside [0, size)
 *   vckd_add(a, b, &r)         r = a + b; false when it does not fit
 *   vckd_mul(a, b, &r)         r = a * b; false when it does not fit
 */
#ifndef VCC_CKD_H
#define VCC_CKD_H

#include <stdbool.h>
#include <stdint.h>

static inline bool vckd_range(uint64_t off, uint64_t n, uint64_t size)
{
	uint64_t end;

	return !__builtin_add_overflow(off, n, &end) && end <= size;
}

/* the sums and products: true when the result is exact */
#define vckd_add(a, b, r) (!__builtin_add_overflow((a), (b), (r)))
#define vckd_mul(a, b, r) (!__builtin_mul_overflow((a), (b), (r)))

#endif
