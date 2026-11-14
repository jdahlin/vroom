/* select: S.f */
/* select: In.h */
/* select: Out.in */
struct In { int h; int i; };
struct Out { int pad; struct In in[2]; };
struct S { int f; };
typedef __SIZE_TYPE__ size_t;
#define offsetof(t, m) __builtin_offsetof(t, m)
_Static_assert(offsetof(struct Out, in[1].h) == 12, "");
_Static_assert(__builtin_offsetof(struct S, f) == 0, "");
