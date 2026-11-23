/* Virtual functions in the constant evaluator ([class.virtual]/16,
   [expr.const]/6 C++20): a virtual constexpr function (declared, an inline
   one, and as a vtable entry as clang -O0's), a call through a base
   reference or pointer going to the final overrider in the object's
   dynamic class, `this` adjusted to the overrider's subobject. */
#include <stdio.h>

struct Base {
    int b = 1;
    constexpr virtual int f() const { return 10 + b; }
    constexpr virtual int g() const { return 20; }
    constexpr int call_f() const { return f(); }
};

struct Middle : Base {
    int m = 2;
    constexpr int f() const override { return 30 + m; }
};

struct Leaf : Middle {
    int l = 3;
    constexpr int g() const override { return 40 + l; }
};

struct Other {
    int o = 5;
    constexpr virtual int h() const { return o; }
};

struct Multi : Other, Base {
    int x = 6;
    constexpr int h() const override { return 100 + x; }
    constexpr int f() const override { return 200 + x; }
};

constexpr int direct()
{
    Base b;
    Leaf l;
    return b.f() + l.f() + l.g();
}

constexpr int through_reference()
{
    Leaf l;
    const Base &r = l;
    const Middle &m = l;
    return r.f() * 1000 + r.g() + m.f();
}

constexpr int through_pointer()
{
    Middle m;
    Base *p = &m;
    return p->f() + p->g();
}

constexpr int from_base_function()
{
    Leaf l;
    return l.call_f();
}

constexpr int second_base()
{
    Multi m;
    const Base &b = m;
    const Other &o = m;
    return b.f() + o.h();
}

static_assert(direct() == 11 + 32 + 43);
static_assert(through_reference() == 32 * 1000 + 43 + 32);
static_assert(through_pointer() == 32 + 20);
static_assert(from_base_function() == 32);
static_assert(second_base() == 206 + 106);

int main()
{
    printf("%d %d %d %d %d\n", direct(), through_reference(),
           through_pointer(), from_base_function(), second_base());
    return 0;
}
