/* select: S.f */
struct S { int f; };
int g(struct S *s) { return s->nosuch + s->f; }
