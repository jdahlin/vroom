// [class.static.data]: C++17 inline static data members that are neither
// const nor constexpr (with constant and dynamic initializers).

static int constructed = 0;

struct Counter {
    int value;
    Counter() : value(7) { constructed++; }
    Counter(int v) : value(v) { constructed++; }
};

struct S {
    static inline int x = 10;
    static inline int y{};
    static inline Counter c{};
    static inline Counter d{42};
};

static Counter g{1};

int main()
{
    if (constructed != 3 || g.value != 1)
        return 1;
    if (S::x != 10)
        return 2;
    S::x += 5;
    if (S::x != 15)
        return 3;
    if (S::y != 0)
        return 4;
    if (S::c.value != 7)
        return 5;
    if (S::d.value != 42)
        return 6;
    return 0;
}
