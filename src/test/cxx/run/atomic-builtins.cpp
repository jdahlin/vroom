// std: c++20
// GCC's __atomic_* and __sync_* builtins (GCC manual 6.55, 6.56) called in function templates and
// functions: the result is by the form (the type the first argument points to, bool or void), as
// libc++'s shared_count does with __atomic_add_fetch
template <class T> T increment(T &t) { return __atomic_add_fetch(&t, 1, __ATOMIC_RELAXED); }
template <class T> T decrement(T &t) { return __atomic_add_fetch(&t, -1, __ATOMIC_ACQ_REL); }
int main()
{
	long a = 5;
	if (increment(a) != 6 || a != 6)
		return 1;
	if (decrement(a) != 5)
		return 2;
	unsigned char b = 250;
	if (__atomic_fetch_add(&b, 10, __ATOMIC_SEQ_CST) != 250 || b != 4)
		return 3;
	int c = 1;
	__atomic_store_n(&c, 9, __ATOMIC_RELEASE);
	if (__atomic_load_n(&c, __ATOMIC_ACQUIRE) != 9)
		return 4;
	int expected = 9;
	if (!__atomic_compare_exchange_n(&c, &expected, 11, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) || c != 11)
		return 5;
	expected = 0;
	if (__atomic_compare_exchange_n(&c, &expected, 12, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) || expected != 11)
		return 6;
	int value = 20, previous = 0;
	__atomic_exchange(&c, &value, &previous, __ATOMIC_SEQ_CST);
	if (previous != 11 || c != 20)
		return 7;
	long d = 3;
	if (__sync_fetch_and_add(&d, 4) != 3 || d != 7 || __sync_bool_compare_and_swap(&d, 7, 1) != true || d != 1)
		return 8;
	int *p = &c;
	if (__atomic_exchange_n(&p, (int *)0, __ATOMIC_SEQ_CST) != &c || p)
		return 9;
	return 0;
}
