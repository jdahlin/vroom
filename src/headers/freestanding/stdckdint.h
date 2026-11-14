/*
 * vcc's <stdckdint.h> (C23 7.20): checked integer arithmetic on the GNU
 * overflow builtins, which take any integer types and store the wrapped result.
 */
#ifndef __VCC_STDCKDINT_H
#define __VCC_STDCKDINT_H

#define __STDC_VERSION_STDCKDINT_H__ 202311L

#define ckd_add(r, a, b) __builtin_add_overflow((a), (b), (r))
#define ckd_sub(r, a, b) __builtin_sub_overflow((a), (b), (r))
#define ckd_mul(r, a, b) __builtin_mul_overflow((a), (b), (r))

#endif
