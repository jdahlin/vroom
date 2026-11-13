struct S { int f : 3; unsigned g : 2; int one : 1; unsigned u1 : 1; _Bool b : 1; long l : 40; };
struct S s0 = { 8, 5 };
void f(struct S *p) {
	p->f = 8;
	p->g = -1;
	p->g = -2;
	p->g = -3;
	p->g = -5;
	p->one = 1;
	p->one = -1;
	p->u1 = 2;
	p->b = 2;
	p->f = -4;
	p->f = -5;
	p->l = 1L << 40;
	p->f += 9;
	struct S t = { .g = 7 };
	(void)t;
	p->f = 7;
}
