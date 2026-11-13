/* select: S.cur next */
/* select: S.len size */
struct S { int cur; int len; };
#define next 1
#define len 2
int f(struct S *s) { return s->cur; }
