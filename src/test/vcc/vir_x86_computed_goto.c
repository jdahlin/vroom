/* Two indirect sites with different live values and direct paths into handlers. */
__attribute__((noinline))
static unsigned dispatch(unsigned seed, int n)
{
	static void *const handlers[] = { &&add, &&mix, &&done };
	unsigned x = seed, y = 3;
	void *target = handlers[seed % 3];
	if (seed & 4)
		goto mix;
	goto *target;
add:
	x += 7 * y;
	if (--n == 0)
		goto done;
	target = handlers[x & 1];
	goto *target;
mix:
	y ^= (x >> 1) + 9;
	x += y;
	if (--n)
		goto *handlers[(x >> 3) & 1];
done:
	return x ^ y;
}

int main(void)
{
	extern int printf(const char *, ...);
	unsigned sum = 0;
	for (unsigned i = 0; i < 128; i++)
		sum = sum * 33 + dispatch(i, 1 + i % 16);
	printf("%u\n", sum);
	return 0;
}
