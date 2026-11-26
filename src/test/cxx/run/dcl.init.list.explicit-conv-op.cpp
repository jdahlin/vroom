// Direct-list-initialization of a class from a single expression of another
// class type with an explicit conversion function to the target class
// (e.g. local_days{year() / month() / day()} in libc++ <chrono>).

struct Dst {
    int x;
    constexpr Dst() : x(0) {}
    constexpr explicit Dst(int v) : x(v) {}
};

struct Src {
    int v;
    constexpr explicit operator Dst() const noexcept { return Dst{v}; }
};

constexpr Dst make_dst(Src s) { return Dst{s}; }

int main()
{
    constexpr Dst d = make_dst(Src{99});
    if (d.x != 99)
        return 1;
    return 0;
}
