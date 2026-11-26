// [dcl.init.ref]/5.4.1, [over.ics.ref]/2: binding a const lvalue reference
// (or rvalue reference) to a prvalue returned by a user-defined conversion
// function (e.g. sys_days{year_month_day} in libc++ <chrono>).

struct Dst {
    int x;
    constexpr Dst() : x(0) {}
    constexpr explicit Dst(int v) : x(v) {}
};

struct Src {
    int v;
    constexpr operator Dst() const noexcept { return Dst{v}; }
};

constexpr Dst make_dst(Src s) { return Dst{s}; }

static int take_rref(Dst &&d) { return d.x; }

int main()
{
    constexpr Dst d = make_dst(Src{42});
    if (d.x != 42)
        return 1;
    if (take_rref(Src{7}) != 7)
        return 2;
    return 0;
}
