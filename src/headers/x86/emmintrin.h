/*
 * vcc's <emmintrin.h>, only the part MinGW-w64's <winnt.h> reaches: the SSE2
 * vector types and the fences, cache-line flush and spin-wait hint its macros
 * name (MemoryFence, LoadFence, CacheLineFlush, YieldProcessor).  The SSE2
 * arithmetic intrinsics are not here yet (TODO.md).
 */
#ifndef __VCC_EMMINTRIN_H
#define __VCC_EMMINTRIN_H

#include <xmmintrin.h>

typedef double __m128d __attribute__((__vector_size__(16), __aligned__(16)));
typedef long long __m128i __attribute__((__vector_size__(16), __aligned__(16)));
typedef double __m128d_u __attribute__((__vector_size__(16), __aligned__(1)));
typedef long long __m128i_u __attribute__((__vector_size__(16), __aligned__(1)));

static __inline__ __attribute__((__always_inline__)) void _mm_lfence(void)
{
	__asm__ __volatile__("lfence" ::: "memory");
}

static __inline__ __attribute__((__always_inline__)) void _mm_mfence(void)
{
	__asm__ __volatile__("mfence" ::: "memory");
}

static __inline__ __attribute__((__always_inline__)) void _mm_clflush(void const *__p)
{
	__asm__ __volatile__("clflush %0" : : "m"(*(const volatile char *)__p) : "memory");
}

static __inline__ __attribute__((__always_inline__)) void _mm_pause(void)
{
	__asm__ __volatile__("pause");
}

#endif
