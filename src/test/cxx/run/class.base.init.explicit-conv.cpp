// [class.base.init], CWG 2327: mem-initializer direct-initializing a class
// member from another class with an explicit conversion function (e.g.
// fmt::fstring's `: str(S())`).

struct SV {
    const char *p;
    constexpr SV(const char *s) : p(s) {}
};

struct Src {
    constexpr explicit operator SV() const { return SV("ok"); }
};

struct Holder {
    SV str;
    Holder(const Src &) : str(Src()) {}
};

int main()
{
    Holder h(Src{});
    if (h.str.p[0] != 'o' || h.str.p[1] != 'k')
        return 1;
    return 0;
}
