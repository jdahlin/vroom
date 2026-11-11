/* Constant bindings belong to each inline call, with normal C side effects. */
static int calls, cleanups;
static int once(int x) { calls++; return x; }
static void clean(int *p) { cleanups += *p; }
static __attribute__((always_inline)) inline int add(int x, int y)
{
	int k = 3;
	return x + y + k;
}
static __attribute__((always_inline)) inline int choose(int x)
{
	int k __attribute__((cleanup(clean))) = 2;
	if (x) goto yes;
	return 7;
yes:
	return x + k;
}
static __attribute__((always_inline)) inline int mutate(int x)
{
	int *p = &x;
	*p += 5;
	return x;
}
static inline int known(int x) { return __builtin_constant_p(x + 1); }
static inline double fadd(double x, double y) { return x + y; }
struct pair { long x, y; };
static __attribute__((always_inline)) inline struct pair pair(long x)
{
	struct pair p = { x, x + 1 };
	return p;
}
int main(void)
{
	volatile int v = 9;
	int k = 6;
	int once_init = 0;
	int comma = (once_init++, 7);
	if (comma != 7 || once_init != 1) return 8;
	if (add(1, 2) != 6 || add(v, 2) != 14 || add(4, 5) != 12) return 1;
	if (add(once(1), add(once(2), once(3))) != 12 || calls != 3) return 2;
	if (choose(0) != 7 || choose(v) != 11 || choose(4) != 6 || cleanups != 6) return 3;
	if (mutate(8) != 13 || mutate(v) != 14) return 4;
	if (fadd(1.5, 2.25) != 3.75) return 5;
	struct pair a = pair(20), b = pair(v);
	if (a.x != 20 || a.y != 21 || b.x != 9 || b.y != 10) return 6;
#ifdef __OPTIMIZE__
	if (!known(12) || !__builtin_constant_p(k)) return 7;
	if (__builtin_constant_p(k) ? 0 : 1) return 9;
	if ((calls++, __builtin_constant_p(k) ? 0 : 1)) return 10;
	if (calls != 4) return 11;
#endif
	return 0;
}
