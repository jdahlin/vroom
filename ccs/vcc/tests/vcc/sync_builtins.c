/*
 * Legacy __sync builtins without an __atomic form: val/bool compare-and-swap
 * (casal), lock_release (release store of 0), synchronize (dmb ish).
 * PostgreSQL's configure probes them; without them it fell back to
 * semaphores (HAVE_GCC__SYNC_* unset).
 */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	signed char c = -1;
	unsigned char uc = 200;
	short h = -2;
	int i = 7;
	long long l = -5;
	void *p = 0;
	char lock = 0;
	int r1 = __sync_val_compare_and_swap(&c, -1, 3);	/* hit: returns -1 sign-extended */
	int r2 = __sync_val_compare_and_swap(&c, -1, 4);	/* miss: returns 3 */
	int r3 = __sync_val_compare_and_swap(&uc, 200, 201);	/* zero-extended */
	int b1 = __sync_bool_compare_and_swap(&h, -2, -9);	/* compares the low 16 bits */
	int b2 = __sync_bool_compare_and_swap(&h, -2, 1);
	int r4 = __sync_val_compare_and_swap(&i, 7, 8);
	long long r5 = __sync_val_compare_and_swap(&l, -5LL, 1LL << 40);
	int b3 = __sync_bool_compare_and_swap(&l, 1LL << 40, -1LL);
	void *r6 = __sync_val_compare_and_swap(&p, (void *)0, (void *)&i);
	int t = __sync_lock_test_and_set(&lock, 1);

	__sync_synchronize();
	__sync_lock_release(&lock);
	printf("%d %d %d %d %d %d %d %d %lld %lld %d %d %d %d\n", r1, r2, c, r3, uc, b1, b2, h, r5, l, b3,
	       r6 == 0 && p == &i, t, lock);
	if (r1 != -1 || r2 != 3 || c != 3 || r3 != 200 || uc != 201 || !b1 || b2 || h != -9 || r4 != 7 || i != 8 ||
	    r5 != -5 || l != -1 || !b3 || r6 || p != &i || t || lock)
		abort();
	return 0;
}
