/* __thread in a static executable (local-exec), as clang: initialised
 * (.tdata), zero (.tbss), static, struct, arrays, address taken */
int printf(const char *, ...);

struct pt { int x; long long y; char c[5]; };
__thread int ti = 42;
__thread long long tl = -5;
__thread int tz;
static __thread short ts = 7;
__thread struct pt tp = { 1, 2, "abcd" };
__thread char tarr[100];
__thread double td = 2.5;
static _Thread_local int tl2[4] = { 9, 8, 7, 6 };

static int *addr_ti(void) { return &ti; }

int main(void)
{
	int bad = 0;
	int *p = addr_ti();

	bad |= ti != 42 || tl != -5 || tz != 0 || ts != 7 || td != 2.5;
	bad |= tp.x != 1 || tp.y != 2 || tp.c[3] != 'd' || tl2[2] != 7;
	*p = 100;
	tz += 3;
	ts *= 2;
	tarr[99] = 5;
	tp.y += ti;
	for (int i = 0; i < 4; i++)
		tl2[i] += i;
	bad |= ti != 100 || tz != 3 || ts != 14 || tarr[99] != 5 || tp.y != 102 || tl2[3] != 9;
	printf("%d %lld %d %d %d %lld %s %d %g\n", ti, tl, tz, ts, tarr[99], tp.y, tp.c, tl2[3], td);
	return bad;
}
