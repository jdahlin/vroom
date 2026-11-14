/* Keep expected and desired live after the implicit accumulator write. */
__attribute__((noinline)) unsigned cas32(unsigned *p, unsigned expected, unsigned desired)
{
	unsigned old = __sync_val_compare_and_swap(p, expected, desired);
	return old + 2 * expected + 3 * desired;
}

__attribute__((noinline)) unsigned long long cas64(unsigned long long *p,
	unsigned long long expected, unsigned long long desired)
{
	return __sync_val_compare_and_swap(p, expected, desired);
}

__attribute__((noinline)) unsigned fetch_update(unsigned *p, unsigned amount)
{
	unsigned old = __atomic_fetch_add(p, amount, __ATOMIC_SEQ_CST);
	return old + __atomic_exchange_n(p, old, __ATOMIC_SEQ_CST) + amount;
}

int main(void)
{
	unsigned x = 7;
	unsigned long long a = 1ull << 44, b = a + 1, y = a;
	if (cas32(&x, 7, 10) != 51 || x != 10) return 1;
	if (cas32(&x, 7, 20) != 84 || x != 10) return 2;
	if (fetch_update(&x, 3) != 26 || x != 10) return 4;
	if (cas64(&y, a, b) != a || y != b) return 3;
	return cas64(&y, a, 0) != b || y != b;
}
