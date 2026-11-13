/* select: S.n */
/* select: S.m */
/* select: S.cat */
/* select: S.qq */
/* select: S.obj */
struct S { int n; int m; int cat; int qq; int obj; };
struct T { int n; };
#define ARG(x) (x)
#define BODY(s) ((s)->n)
#define OBJ_BODY .m
#define obj (s->obj)
#define NESTED(s) BODY(s)
#define PASTE(s) ((s)->ca ## t)
#define STR(f) #f
#define SPL(s) ((s)->q\
q)
int f(struct S *s)
{
	return ARG(s->m) + BODY(s) + (*s)OBJ_BODY + NESTED(s) + PASTE(s) + sizeof STR(n) + SPL(s) + obj;
}
