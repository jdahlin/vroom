/* [dcl.fct.spec]/3, [class.ctor]/1, [dcl.inline]: explicit and inline
   together on a constructor, in either order, defined in the class or
   declared there and defined after it (libc++'s <chrono> and <valarray>) */
struct Day {
    unsigned char d;
    Day() = default;
    explicit inline constexpr Day(unsigned v) noexcept : d((unsigned char)v) {}
    inline explicit Day(long v, int) noexcept : d((unsigned char)(v + 1)) {}
};
struct Array {
    unsigned long n;
    inline explicit Array(unsigned long k);
    inline ~Array();
};
/* an attribute between the specifiers (libc++'s _LIBCPP_HIDE_FROM_ABI) */
struct Tagged {
    int t;
    inline __attribute__((abi_tag("tag"))) explicit Tagged(int v) : t(v) {}
    explicit __attribute__((visibility("hidden"))) Tagged(long v, int)
        : t((int)v)
    {
    }
};
inline Array::Array(unsigned long k) : n(k) {}
inline Array::~Array() {}

static int run(unsigned seven)
{
    Day a(seven);
    Day b(7L, 1);
    Array c(3);
    Tagged t(1);
    Tagged u(2L, 0);
    if (t.t + u.t != 3)
        return 1;
    return a.d + b.d + (int)c.n - 18;
}

int main(int argc, char **)
{
    return run(7 + argc - 1);
}
