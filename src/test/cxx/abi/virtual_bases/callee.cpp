#include "shared.h"

int trace[16];
int traced;

A::A(int a) : a(a)
{
    trace[traced++] = 1;
}

A::~A()
{
    trace[traced++] = -1;
}

int A::value() const
{
    return a;
}

B::B(int b) : A(100), b(b)
{
    trace[traced++] = 2;
}

B::~B()
{
    trace[traced++] = -2;
}

int B::from_b() const
{
    return b * 10 + a;
}

C::C(int c) : A(200), c(c)
{
    trace[traced++] = 3;
}

C::~C()
{
    trace[traced++] = -3;
}

int C::from_c() const
{
    return c * 10 + a + value();
}

int value_of(const A *a)
{
    return a->value();
}

int b_of(const B &b)
{
    return b.from_b() + b.a;
}

int c_of(const C *c)
{
    return c->from_c();
}

int a_through_c(const C *c)
{
    return c->a;
}

static B the_b(7);

const B *callee_b()
{
    return &the_b;
}
