/* select: S.f */
/* select: S.g */
/* select: In.h */
/* select: Out.in */
struct In { int h; int i; };
struct Out { struct In in; int arr[4]; struct In ar[3]; union { int g; long l; }; };
struct S { int f; int g; };
struct S s1 = { .f = 1, .g = 2 };
struct S s2 = { g: 2, f: 1 };
struct Out o1 = { .in.h = 1, .ar[1].h = 2, .ar[0 ... 2].i = 3, .g = 4 };
struct Out o2 = { .in = { .h = 1 } };
struct S *mk(void) { return &(struct S){ .f = 3 }; }
