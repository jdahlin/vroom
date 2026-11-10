/* D1.0: builtin argument errors give the type and the expected count */
int f(int *p, int x) {
	__atomic_load_n(x, 0);
	__builtin_add_overflow(1, 2, x);
	return __builtin_expect(1);
}
