/* A repeated load of the same address reuses the first one's register, but
 * not across a store that may alias it, a call, a barrier or a volatile access. */

struct S { long a, b; int c; unsigned char d; signed char e; };

__attribute__((noinline)) long same(struct S *p) { return p->b + p->b * 3 + p->b; }

__attribute__((noinline)) long alias(struct S *p, long *q)
{
	long x = p->b;
	*q = 77;			/* may be p->b */
	return x * 1000 + p->b;
}

__attribute__((noinline)) void bump(struct S *p) { p->a += 5; }

__attribute__((noinline)) long viacall(struct S *p)
{
	long x = p->a;
	bump(p);
	return x * 1000 + p->a;
}

__attribute__((noinline)) long vol(volatile long *p)
{
	long x = *p;
	long y = *p;
	return x * 1000 + y;
}

__attribute__((noinline)) long barrier(struct S *p)
{
	long x = p->a;
	__asm__ volatile("" ::: "memory");
	p->a = 9;
	__asm__ volatile("" ::: "memory");
	return x * 1000 + p->a;
}

__attribute__((noinline)) long widths(struct S *p)
{
	long r = p->c;			/* sign-extended */
	r += (unsigned)p->c;		/* zero-extended: another extension, not reused */
	r += p->d + p->e + p->d + p->e;
	return r;
}

__attribute__((noinline)) long stored(struct S *p, long v)
{
	p->a = v;			/* the store's register is the value */
	long x = p->a;
	p->a = v + 1;
	return x * 1000 + p->a;
}

__attribute__((noinline)) long clobber_base(struct S **pp)
{
	struct S *p = *pp;
	long x = p->a;
	p = *pp = (struct S *)((char *)p + 0);	/* same pointer, base reloaded */
	return x + p->a;
}

__attribute__((noinline)) long other_base(struct S *p, struct S *q)
{
	long x = p->a;
	q->a = 1;
	return x * 1000 + p->a;
}

int main(void)
{
	struct S s = { 4, 5, -2, 200, -3 };
	long v = 11;

	if (same(&s) != 25) return 1;
	if (alias(&s, &s.b) != 5 * 1000 + 77) return 2;
	if (viacall(&s) != 4 * 1000 + 9) return 3;
	if (vol(&v) != 11011) return 4;
	if (barrier(&s) != 9 * 1000 + 9) return 5;
	s.a = 4, s.b = 5;
	if (widths(&s) != -2 + 4294967294L + 2 * (200 - 3)) return 6;
	if (stored(&s, 40) != 40 * 1000 + 41) return 7;
	struct S *ps = &s;
	if (clobber_base(&ps) != 82) return 8;
	if (other_base(&s, &s) != 41 * 1000 + 1) return 9;
	return 0;
}
