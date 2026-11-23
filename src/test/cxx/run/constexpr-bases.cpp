/* Base classes in the constant evaluator: the base subobject is a member
   of the class ([class.derived]/2), a pointer or reference converted to
   it ([conv.ptr]/3, [dcl.init.ref]) and back ([expr.static.cast]/11), the
   implicit default and copy constructors of a class with bases, an
   object copied after a constructor initialized its members (the
   members' storage outlives the constructor's call). */
#include <stdio.h>

struct A {
    int a = 1;
    constexpr int f() const { return a; }
};

struct B : A {
    int b = 2;
    constexpr int h() const { return f() + b; }
};

struct C {
    int c = 3;
};

struct D : A, C {
    int d = 4;
};

struct Fixed {
    int v;
    constexpr Fixed(int x) : v(x) {}
    constexpr int get() const { return v; }
};

struct Derived : Fixed {
    constexpr Derived() : Fixed(9) {}
};

struct Empty {};
struct WithEmpty : Empty {
    int n = 6;
};

struct Inner {
    int a = 1;
};
struct Outer {
    int b = 2;
    Inner x;
};

constexpr int through_reference()
{
    B b;
    A &r = b;
    r.a = 7;
    return b.a + b.b;
}

constexpr int through_pointer()
{
    B b;
    A *p = &b;
    p->a = 5;
    B *back = static_cast<B *>(p);
    return back->b + back->a;
}

constexpr int second_base()
{
    D d;
    C &c = d;
    A &a = d;
    return c.c + a.a + d.d;
}

constexpr int member_function() { B b; return b.h(); }

constexpr int copied()
{
    B b;
    B c = b;
    c.a = 5;
    return c.a * 10 + b.a;
}

constexpr int copied_nested()
{
    Outer o;
    Outer p = o;
    p.x.a = 8;
    return p.x.a * 10 + o.x.a;
}

constexpr Derived made = Derived();

static_assert(through_reference() == 9);
static_assert(through_pointer() == 7);
static_assert(second_base() == 8);
static_assert(member_function() == 3);
static_assert(copied() == 51);
static_assert(copied_nested() == 81);
static_assert(made.get() == 9 && made.v == 9);
static_assert(WithEmpty().n == 6);

int main()
{
    printf("%d %d %d %d %d %d\n", through_reference(), through_pointer(),
           second_base(), copied(), copied_nested(), made.get());
    return 0;
}
