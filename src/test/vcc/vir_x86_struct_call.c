/* SysV register classes, stack rollback and the hidden destination pointer. */
typedef struct { long long x, y; } pair;
typedef struct { double x; long long y; } mixed;
typedef struct { long long x, y, z; } large;

__attribute__((noinline)) pair swap_pair(pair p) { return (pair){p.y, p.x}; }
__attribute__((noinline)) mixed change_mixed(mixed m) { return (mixed){m.x + 2, m.y - 3}; }

/* With only one integer argument register left, both halves of p must go
 * on the stack. tail can still use the last register. */
__attribute__((noinline)) long long crowded(long long a, long long b, long long c,
	long long d, long long e, pair p, long long tail)
{
	return a + 2*b + 3*c + 4*d + 5*e + 6*p.x + 7*p.y + 8*tail;
}

__attribute__((noinline)) large change_large(long long k, large v)
{
	v.x += k;
	return (large){v.z, v.y, v.x};
}

int main(void)
{
	pair p = swap_pair((pair){17, 29});
	mixed m = change_mixed((mixed){1.25, 20});
	large v = {3, 5, 7}, result = change_large(10, v);
	return p.x != 29 || p.y != 17 || m.x != 3.25 || m.y != 17 ||
		result.x != 7 || result.y != 5 || result.z != 13 || v.x != 3 ||
		crowded(1, 2, 3, 4, 5, (pair){6, 7}, 8) != 204;
}
