/* [expr.prim.lambda]: non-generic lambdas: captures by copy and by
   reference, defaults, this and *this, init-captures, mutable, deduced
   and trailing return types, the conversion to a pointer to function,
   closures passed to templates, nested lambdas, lambdas in inline
   functions and member functions (their closures' symbols numbered as
   Ul...E_, the others' as clang's $_N) */

#define check(e)                                                               \
    do {                                                                       \
        if (!(e))                                                              \
            __builtin_abort();                                                 \
    } while (0)

/* not constants: clang folds a condition whose call is a constant
   expression (a captureless lambda's call operator is constexpr), and
   then needs no symbol for it */
int zero = 0;

template <class F> int call(F f) { return f(1); }
template <class F> int twice(F f) { return f() + f(); }

struct S {
    int v;
    int get()
    {
        auto l = [this] { return v; };
        return l();
    }
    int bump()
    {
        auto l = [=] { return ++v; }; /* this, captured by [=] (C++17) */
        l();
        return v;
    }
    int copy()
    {
        auto l = [*this]() mutable {
            v += 10;
            return v;
        };
        int r = l();
        return r + v; /* the copy's v changed, not ours */
    }
    int nested()
    {
        return [&] { return [this] { return v * 2; }(); }();
    }
};

inline int in_inline(int a)
{
    return [](int x) { return x + 1; }(a) + [a](int x) { return x * a; }(2);
}

struct Big {
    int a[8];
};

int by_value(Big b)
{
    auto l = [b] { return b.a[0] + b.a[7]; };
    b.a[0] = 100; /* the capture is a copy */
    return l();
}

/* a class whose copy constructor is user-provided: the closure's copy
   constructor is not trivial, a function of its own (in an inline
   function Ul...E_'s, in another $_N's) */
struct Counted {
    int v;
    Counted(int x) : v(x) {}
    Counted(const Counted &o) : v(o.v + 100) {}
};

inline int copy_counted()
{
    Counted t(5);
    auto l = [t] { return t.v; };
    auto l2 = l;
    return l() + l2();
}

int copy_counted_here()
{
    Counted t(zero + 6);
    auto l = [t] { return t.v; };
    auto l2 = l;
    return l() + l2();
}

/* captured by reference in a lambda whose enclosing lambda captured it by
   copy: the enclosing closure's member, const unless that lambda is
   mutable */
int nested_by_reference()
{
    int y = 2 + zero;
    auto m = [y] { return [&] { return y; }(); };
    auto m2 = [=] { return [&] { return y; }(); };
    auto m3 = [y]() mutable {
        [&] { y++; }();
        return y;
    };
    auto m4 = [y] { return [&y] { return y; }(); };
    auto m5 = [&] { return [=] { return [&] { return y; }(); }(); };
    int &r = y;
    auto m6 = [r] { return [&] { return r; }(); };
    int a = m3();
    int b = m3();
    return m() + m2() * 10 + a * 100 + b * 1000 + m4() * 10000 +
           m5() * 100000 + m6() * 1000000 + (y - 2);
}

int main()
{
    int x = 3, y = 4;
    auto none = [] { return 1; };
    check(none() + zero == 1);
    auto id = [](int q) { return q; };
    check(id(7 + zero) == 7);
    auto copy = [x](int q) { return q + x; };
    x = 100;
    check(copy(1) == 4);
    x = 3;
    auto ref = [&y](int q) {
        y += q;
        return y;
    };
    check(ref(1) == 5 && y == 5);
    auto all_copy = [=] { return x + y; };
    auto all_ref = [&] { return x + y; };
    y = 10;
    check(all_copy() == 8 && all_ref() == 13);
    auto init = [z = x + 1, &w = y](long) { return z + w; };
    check(init(0) == 14);
    auto counter = [n = 0]() mutable { return ++n; };
    counter();
    check(counter() == 2);
    auto trailing = [](int a) -> long { return a * 2L; };
    check(trailing(21 + zero) == 42);
    auto mixed = [=, &y](int a) {
        y += a;
        return x + y;
    };
    check(mixed(1) == 14 && y == 11);

    int (*fp)(int) = id;
    check(fp(9) == 9);
    int (*fp2)(int) = [](int a) { return a - 1; };
    check(fp2(1) == 0);
    void (*vp)(int *) = [](int *p) { *p = 5; };
    int out = 0;
    vp(&out);
    check(out == 5);

    check(call(copy) == 4);
    check(call([](int a) { return a + 41; }) == 42);
    check(twice([&] { return ++x; }) == 9 && x == 5);

    auto nested = [x] { return [x] { return x * 3; }(); };
    check(nested() + zero == 15);

    /* a reference captured by reference: the object it refers to; by
       copy: a copy of that object; through an enclosing lambda's default */
    int &r = y;
    auto through = [&] { return [&] { return ++r + x; }(); };
    auto copied = [r]() mutable { return ++r; };
    check(through() == 17 && y == 12 && copied() == 13 && y == 12);
    /* closures copied: their captures with them */
    auto again = copy;
    decltype(copy) third = again;
    check(third(0) == 3 && sizeof third == sizeof(int));

    S s{1};
    check(s.get() == 1);
    check(s.bump() == 2);
    check(s.copy() == 14);
    check(s.nested() == 4);
    check(in_inline(2) == 7);
    Big b = {{1, 0, 0, 0, 0, 0, 0, 2}};
    check(by_value(b) == 3);
    check(copy_counted() == 105 + 205 && copy_counted_here() == 106 + 206);
    check(nested_by_reference() == 2222432);
    return 0;
}
