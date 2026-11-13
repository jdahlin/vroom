/*
 * The per-thread stack limit behind vstack_low() (vstack.h).
 */
#include <pthread.h>
#include "port/vstack.h"

_Thread_local uintptr_t vstack_lim;

void vstack_init(void)
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
		vstack_lim = 1;	/* unknown: never low */
		return;
	}
	pthread_attr_getstack(&a, &addr, &sz);
	pthread_attr_destroy(&a);
	lo = (uintptr_t)addr;
#endif
	vstack_lim = lo + VSTACK_RESERVE;
}
