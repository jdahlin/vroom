// p->~T() of a scalar T destroys nothing but evaluates p: (--e)->~T() steps e (libc++'s valarray::__clear looped
// forever when the whole expression was dropped)
#include <stdio.h>
typedef unsigned char uc;
template <class T> int f(T *b, T *e) { int n = 0; while (e != b) { (--e)->~T(); n++; } return n; }
int main() { uc a[4]; printf("%d\n", f(a, a + 4)); return f(a, a + 4) == 4 ? 0 : 1; }
