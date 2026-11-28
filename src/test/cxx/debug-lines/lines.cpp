// bench/cxx-debug-lines.py: -g's line table has a row for each line marked
// "row", in the marked file, as clang's has
#include "lines.h"
#define ADD(y) y += half(y)

struct S {
    int v;
    S() : v(1)
    {
    }
};

template <class T> T twice(T x)
{
    return x + x; // row
}

int f(int x)
{
    int a = x * 3; // row

    if (a > 2) // row
        a += 1; // row
    for (int i = 0; i < x; ++i) { // row
        a += i; // row
    }
    while (a > 100) // row
        a /= 2; // row
    S s; // row
    ADD(a); // row
    a += twice(s.v); // row
    return a; // row
}
