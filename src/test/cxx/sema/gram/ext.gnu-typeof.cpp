// [ext:gnu_typeof] typeof, __typeof and __typeof__ of an expression or a
// type-id: one per line, each verdict compared with clang's.  The type of
// an expression is never a reference, a type-id keeps its qualifiers.
int g;
int &r = g;
const int c = 1;
__typeof(g) a1;
__typeof__(r) a2;
__typeof(int *) a3;
__typeof(c) *a4 = &c;
int *a5 = &a1;
__typeof(r) *a6 = &g;
int &a7 = a2;
__typeof(c) a8;
__typeof(g, c) a9 = 0;
__typeof(g) &a11 = g;
void f(__typeof(g) x, __typeof(g) y);
