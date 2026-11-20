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
inline Array::Array(unsigned long k) : n(k) {}
inline Array::~Array() {}

static int run(unsigned seven)
{
    Day a(seven);
    Day b(7L, 1);
    Array c(3);
    return a.d + b.d + (int)c.n - 18;
}

int main(int argc, char **)
{
    return run(7 + argc - 1);
}
