/* stack guard for the recursive parsers: deeply nested input ends the TU with
 * a fatal error instead of overflowing the stack (and, under vccd, killing
 * the server).  vstack_guard_low() is true once less than VSTACK_GUARD_RESERVE bytes of
 * this thread's stack are left; it costs a TLS load and a compare. */
#ifndef VCC_PORT_STACK_GUARD_H
#define VCC_PORT_STACK_GUARD_H
#include <pthread.h>
#include <stdint.h>

#define VSTACK_GUARD_RESERVE (128u << 10)
extern _Thread_local uintptr_t vstack_guard_limit;
void vstack_guard_init(void);

static inline int vstack_guard_low(void)
{
	char here;

	if (__builtin_expect(!vstack_guard_limit, 0))
		vstack_guard_init();
	return (uintptr_t)&here < vstack_guard_limit;
}
#endif
