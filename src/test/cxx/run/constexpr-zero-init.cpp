/* Zero- and value-initialization in the constant evaluator ([dcl.init]/6,
   /8): a class with a base, with a constructor, with an unnamed bit-field
   in its aggregate list. */
#include <stdio.h>

struct Base {
    int a;
    int b;
};

struct Derived : Base {
    int c;
};

struct WithCtor {
    int v;
    int w;
    constexpr WithCtor() : v(3), w(4) {}
};

struct Mixed {
    int x;
    int : 0;
    int y;
};

constexpr Derived d = {};
static_assert(d.a == 0 && d.b == 0 && d.c == 0);

constexpr Derived e = {{1, 2}, 3};
static_assert(e.a == 1 && e.b == 2 && e.c == 3);

constexpr int zeros()
{
    Derived z{};
    Mixed m{};
    return z.a + z.b + z.c + m.x + m.y;
}
static_assert(zeros() == 0);

constexpr Mixed mx = {5, 6};
static_assert(mx.x == 5 && mx.y == 6);

constexpr WithCtor wc = WithCtor();
static_assert(wc.v == 3 && wc.w == 4);

int main()
{
    printf("%d %d %d %d\n", d.c, e.b, mx.y, wc.w);
    return 0;
}
