int a = 2147483647 + 1;
long b = 65536 * 65536;
void f(int x) {
	int c = 2147483647 + 1;
	int d = 65536 * 65536;
	int e = -(-2147483647 - 1);
	unsigned u = 4294967295u + 1;
	int g = x + 2147483647 * 2;
	long h = 9223372036854775807L + 1;
	int i = (int)2147483648LL;
	(void)c; (void)d; (void)e; (void)u; (void)g; (void)h; (void)i;

	int j = -2147483647 - 1;
	int k = 100000 * 100000 / 10;
	(void)j; (void)k;
}
