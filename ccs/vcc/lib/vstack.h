/* stack guard for the recursive parsers: deeply nested input ends the TU with
 * a fatal error instead of overflowing the stack (and, under vccd, killing
 * the server).  vstack_low() is true once less than VSTACK_RESERVE bytes of
 * this thread's stack are left; it costs a TLS load and a compare. */
#ifndef VCC_VSTACK_H
#define VCC_VSTACK_H
#include <pthread.h>
#include <stdint.h>

#define VSTACK_RESERVE (128u << 10)
extern _Thread_local uintptr_t vstack_lim;
void vstack_init(void);

static inline int vstack_low(void)
{
	char here;

	if (__builtin_expect(!vstack_lim, 0))
		vstack_init();
	return (uintptr_t)&here < vstack_lim;
}
#endif
