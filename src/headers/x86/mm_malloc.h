/*
 * vcc's <mm_malloc.h>: _mm_malloc and _mm_free, which MinGW-w64's <malloc.h>
 * expects from the compiler.  On Windows they are mingw-w64's aligned
 * allocator (declared by <malloc.h> before it includes this header); elsewhere
 * posix_memalign and free.
 */
#ifndef __VCC_MM_MALLOC_H
#define __VCC_MM_MALLOC_H

#include <stdlib.h>
#ifdef _WIN32
#include <malloc.h>
#else
int posix_memalign(void **, size_t, size_t);
#endif

#ifndef _mm_malloc
static __inline__ void *_mm_malloc(size_t __size, size_t __align)
{
	void *__p;

	if (__align == 1)
		return malloc(__size);
	if (!(__align & (__align - 1)) && __align < sizeof(void *))
		__align = sizeof(void *);
#ifdef __MINGW32__
	__p = __mingw_aligned_malloc(__size, __align);
#else
	if (posix_memalign(&__p, __align, __size))
		__p = 0;
#endif
	return __p;
}

static __inline__ void _mm_free(void *__p)
{
#ifdef __MINGW32__
	__mingw_aligned_free(__p);
#else
	free(__p);
#endif
}
#endif

#endif
