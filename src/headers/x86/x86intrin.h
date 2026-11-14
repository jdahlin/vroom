/*
 * vcc's <x86intrin.h>, only the part MinGW-w64's <winnt.h> and <intrin.h>
 * reach: the time-stamp counter and, through <emmintrin.h>, the fences and the
 * spin-wait hint.  The rest of the x86 intrinsics (<immintrin.h> and the
 * per-feature headers) are not here yet (TODO.md).
 */
#ifndef __VCC_X86INTRIN_H
#define __VCC_X86INTRIN_H

#include <emmintrin.h>

static __inline__ __attribute__((__always_inline__)) unsigned long long __rdtsc(void)
{
	unsigned int __lo, __hi;

	__asm__ __volatile__("rdtsc" : "=a"(__lo), "=d"(__hi));
	return (unsigned long long)__hi << 32 | __lo;
}

static __inline__ __attribute__((__always_inline__)) unsigned long long __rdtscp(unsigned int *__aux)
{
	unsigned int __lo, __hi;

	__asm__ __volatile__("rdtscp" : "=a"(__lo), "=d"(__hi), "=c"(*__aux));
	return (unsigned long long)__hi << 32 | __lo;
}

#define _rdtsc() __rdtsc()

#endif
