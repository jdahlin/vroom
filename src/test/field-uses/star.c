/* select: S.* */
struct S { int a; int b; struct { int c; } in; };
struct T { int a; };
int f(struct S *s, struct T *t) { return s->a + s->b + s->in.c + t->a; }
