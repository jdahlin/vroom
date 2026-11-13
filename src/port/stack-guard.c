/*
 * The per-thread stack limit behind vstack_guard_low() (vstack.h).
 */
#include <pthread.h>
#include "port/stack-guard.h"

_Thread_local uintptr_t vstack_guard_limit;

void vstack_guard_init(void)
{
	pthread_t t = pthread_self();
	uintptr_t lo;
#ifdef __APPLE__
	lo = (uintptr_t)pthread_get_stackaddr_np(t) - pthread_get_stacksize_np(t);
#else
	pthread_attr_t a;
	void *addr;
	size_t sz;

	if (pthread_getattr_np(t, &a)) {
		vstack_guard_limit = 1;	/* unknown: never low */
		return;
	}
	pthread_attr_getstack(&a, &addr, &sz);
	pthread_attr_destroy(&a);
	lo = (uintptr_t)addr;
#endif
	vstack_guard_limit = lo + VSTACK_GUARD_RESERVE;
}
