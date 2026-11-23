/* The implicit copy and move assignment and the implicit copy
   constructor in the constant evaluator ([class.copy.assign]/10,
   [class.copy.ctor]/14): members, bases and arrays one by one, into the
   storage the target has (the copy does not alias the original). */
#include <stdio.h>

struct P {
    int a = 1;
    int b = 2;
};

struct A {
    int a = 1;
};

struct D : A {
    int b = 2;
};

struct Arr {
    int v[3];
};

struct Nested {
    P p;
    int n = 3;
};

constexpr int copy_assignment()
{
    P x;
    P y;
    y.a = 9;
    x = y;
    y.a = 3;
    return x.a * 10 + x.b + y.a * 100;
}

constexpr int move_assignment()
{
    P x;
    P y;
    y.b = 7;
    x = static_cast<P &&>(y);
    return x.a + x.b;
}

constexpr int with_base()
{
    D x;
    D y;
    y.a = 9;
    y.b = 8;
    x = y;
    D z = y;
    z.a = 0;
    return x.a + x.b + z.a + y.a;
}

constexpr int array_member()
{
    Arr x{{1, 2, 3}};
    Arr y{{0, 0, 0}};
    y = x;
    x.v[0] = 7;
    return y.v[0] + y.v[1] + y.v[2];
}

constexpr int nested()
{
    Nested a;
    Nested b;
    b.p.a = 5;
    b.n = 6;
    a = b;
    b.p.a = 0;
    return a.p.a * 10 + a.n;
}

constexpr int copy_constructor()
{
    Nested a;
    a.p.b = 4;
    Nested b(a);
    b.p.b = 9;
    return a.p.b * 10 + b.p.b;
}

static_assert(copy_assignment() == 392);
static_assert(move_assignment() == 8);
static_assert(with_base() == 9 + 8 + 0 + 9);
static_assert(array_member() == 6);
static_assert(nested() == 56);
static_assert(copy_constructor() == 49);

int main()
{
    printf("%d %d %d %d %d %d\n", copy_assignment(), move_assignment(),
           with_base(), array_member(), nested(), copy_constructor());
    return 0;
}
