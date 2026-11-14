/*
 * vcc's <xmmintrin.h>, only the part MinGW-w64's <windows.h> and <intrin.h>
 * reach (winnt.h includes the x86 intrinsic headers unconditionally): the
 * __m128 type, the store fence and MXCSR access.  The SSE arithmetic
 * intrinsics are not here yet (TODO.md); a program that wants them should
 * find them missing at compile time, not get clang's.
 */
#ifndef __VCC_XMMINTRIN_H
#define __VCC_XMMINTRIN_H

#include <mm_malloc.h>

typedef float __m128 __attribute__((__vector_size__(16), __aligned__(16)));
typedef float __m128_u __attribute__((__vector_size__(16), __aligned__(1)));

static __inline__ __attribute__((__always_inline__)) void _mm_sfence(void)
{
	__asm__ __volatile__("sfence" ::: "memory");
}

static __inline__ __attribute__((__always_inline__)) unsigned int _mm_getcsr(void)
{
	unsigned int __csr;

	__asm__ __volatile__("stmxcsr %0" : "=m"(__csr));
	return __csr;
}

static __inline__ __attribute__((__always_inline__)) void _mm_setcsr(unsigned int __csr)
{
	__asm__ __volatile__("ldmxcsr %0" : : "m"(__csr));
}

#endif
