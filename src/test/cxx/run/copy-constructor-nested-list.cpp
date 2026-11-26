/* [over.best.ics]/4: in T{{x}} the copy and move constructors of T take no
   user-defined conversion from the inner list, so T(S) is chosen */
extern "C" int printf(const char *, ...);

struct S {
    int n = 0;
    S() = default;
    S(const char *p) : n(p[0]) {}
};

struct T {
    S s{};
    int how = 0;
    T() = default;
    T(S v) : s(v), how(1) {}
};

int main()
{
    auto a = T{{"non-pod"}};
    T b{{}};
    printf("%d %d %d %d\n", a.s.n, a.how, b.s.n, b.how);
    return 0;
}
