/* select: Outer.a */
/* select: S.pos.x */
/* select: fd.v */
/* select: Dup.k */
/* select: Tagged.t */
struct Outer { int z; union { struct { int a; int b; }; long c; }; };
struct S { struct { int x, y; } pos; int x; };
typedef struct { int v; } fd;
struct Tagged { int t; };
struct Dup { int k; };
int f(struct Outer *o, struct S *s, fd *p, struct Tagged *t)
{
	struct Dup { int k; int j; } d = { 1, 2 };
	struct { int v; } loose;

	loose.v = 1;
	return o->a + s->pos.x + s->x + p->v + t->t + d.k + loose.v;
}
