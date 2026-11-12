/* A local's stored value is not carried past a store through a pointer or a call
   just because its address has not been taken yet in the text: a loop runs that
   code again after the address is taken further down (VIR's builder walks in text
   order; its store table kept the entry).  Edge cases: a pointer store, a call that
   writes through a saved pointer, a goto loop, a field of a struct local, a slot
   whose address is never taken (still forwarded: the result must not change), a
   nested loop where the address escapes in the outer one. */
#define NI __attribute__((noinline))

static int *gp;
NI void poke(void) { if (gp) *gp = 100; }

NI int ptr_store(int n, int *d)
{
	int x = 0, *p = d, s = 0;

	for (int i = 0; i < n; i++) {
		x = i;
		*p = 100;
		s += x;
		p = &x;
	}
	return s;
}
NI int call_store(int n)
{
	int x = 0, s = 0;

	gp = 0;
	for (int i = 0; i < n; i++) {
		x = i;
		poke();
		s += x;
		gp = &x;
	}
	return s;
}
NI int goto_loop(int n, int *d)
{
	int x = 0, *p = d, s = 0, i = 0;
again:
	x = i;
	*p = 7;
	s += x;
	p = &x;
	if (++i < n)
		goto again;
	return s;
}
struct P { int a, b; };
NI int field(int n, int *d)
{
	struct P q = { 0, 0 };
	int *p = d, s = 0;

	for (int i = 0; i < n; i++) {
		q.a = i, q.b = -i;
		*p = 50;
		s += q.a + q.b;
		p = &q.b;
	}
	return s;
}
NI int never_taken(int n, int *d)
{
	int x = 0, s = 0;

	for (int i = 0; i < n; i++) {
		x = i;
		*d = 100;
		s += x;
	}
	return s;
}
NI int nested(int n, int *d)
{
	int x = 0, *p = d, s = 0;

	for (int i = 0; i < n; i++) {
		for (int j = 0; j < 2; j++) {
			x = i + j;
			*p = 9;
			s += x;
		}
		p = &x;
	}
	return s;
}

int main(void)
{
	int d = 0;

	if (ptr_store(4, &d) != 0 + 100 * 3) return 1;
	if (call_store(4) != 0 + 100 * 3) return 2;
	if (goto_loop(3, &d) != 0 + 7 + 7) return 3;
	if (field(3, &d) != 0 + (1 + 50) + (2 + 50)) return 4;
	if (never_taken(4, &d) != 6 || d != 100) return 5;
	if (nested(2, &d) != (0 + 1) + (9 + 9)) return 6;
	return 0;
}
