/* [expr.prim.lambda.capture]/10, [expr.prim.this]: this, *this, [=] with
   this, nested lambdas, default member initializers */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

struct S {
    int v = 3;
    int by_this() { return [this] { return v + 1; }(); }
    int by_ref() { return [&] { return v + 2; }(); }
    int by_copy_this() { return [*this] { return v + 3; }(); }
    int mut_copy() { return [*this]() mutable { v += 10; return v; }(); }
    int nested() { return [this] { return [this] { return v * 2; }(); }(); }
    int nested_star() { return [*this] { return [*this] { return v * 3; }(); }(); }
    int nested_mixed() { return [&] { return [=] { return v + 5; }(); }(); }
    int member_call() { return [this] { return by_this(); }(); }
    int init = [this] { return v + 100; }();
    int w = [&] { return v + 200; }();
};

/* a default member initializer analysed by each constructor: one closure type */
struct Two {
    int a = [] { return 1; }();
    int b = [this] { return a + 1; }();
    Two() {}
    Two(int x) : a(x) {}
};

template <class T> struct Tpl {
    T v = [] { return T(7); }();
    T w = [this] { return v + T(1); }();
};

int main()
{
    S s;
    check(s.by_this() == 4);
    check(s.by_ref() == 5);
    check(s.by_copy_this() == 6);
    check(s.mut_copy() == 13 && s.v == 3);
    check(s.nested() == 6);
    check(s.nested_star() == 9);
    check(s.nested_mixed() == 8);
    check(s.member_call() == 4);
    check(s.init == 103 && s.w == 203);
    s.v = 8;
    auto f = [c = s] { return c.v; };
    check(f() == 8);
    Two t1, t2(5);
    check(t1.a == 1 && t1.b == 2 && t2.a == 5 && t2.b == 6);
    Tpl<int> ti;
    Tpl<long> tl;
    check(ti.v == 7 && ti.w == 8 && tl.v == 7 && tl.w == 8);
    return 0;
}
