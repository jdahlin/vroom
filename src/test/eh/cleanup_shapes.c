/* Cleanup variables around the shapes of call the VIR builder turns into invokes: a result in two
   registers, a call through a pointer, a call in a loop left by break and by goto, in a switch, a
   variadic call, and a function with cleanups that returns normally after a caught exception
   further down (cleanup_shapes.cpp, built by clang++, throws and catches). */
#include <stdarg.h>

void cxx_throw(int k);
int s_order[32], s_n;

typedef struct {
    long a, b;
} pair;

static void note(int *p)
{
    s_order[s_n++] = *p;
}

__attribute__((noinline)) static pair two(int k)
{
    if (k == 1)
        cxx_throw(1);
    pair r = {k, k + 1};
    return r;
}

__attribute__((noinline)) static int sum(int n, ...)
{
    va_list ap;
    int s = 0;

    va_start(ap, n);
    for (int i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(ap);
    if (s == 6)
        cxx_throw(6);
    return s;
}

static void (*volatile through)(int) = cxx_throw;

__attribute__((noinline)) int shapes(int k)
{
    int a __attribute__((cleanup(note))) = 1;
    pair p = two(k == 1); /* k 1: throws from a call with a two-register result */
    int total = (int)(p.a + p.b);

    for (int i = 0; i < 4; i++) {
        int b __attribute__((cleanup(note))) = 10 + i;
        if (i == 1 && k == 2)
            break;
        if (i == 2 && k == 3)
            goto out;
        if (i == 3 && k == 4)
            through(4); /* through a pointer */
    }
    switch (k) {
    case 5: {
        int c __attribute__((cleanup(note))) = 20;
        total += sum(3, 1, 2, 3); /* variadic: throws (6) */
        break;
    }
    default:
        total += sum(2, 1, 1);
        break;
    }
out:
    return total;
}
