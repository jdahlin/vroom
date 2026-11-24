// [class.base.init]/2: the distinct members of an anonymous struct share
// its unnamed object; each mem-initializer runs, in the order written
struct S {
    union {
        struct { int a; int b; };
        int c;
    };
    constexpr S(int x) : a(x), b(2) {}
};
constexpr S s(1);
static_assert(s.a == 1, "");
static_assert(s.b == 2, "");
