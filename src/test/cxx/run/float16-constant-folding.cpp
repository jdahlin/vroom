/* [ext:clang_float16]: _Float16 constants are rounded to binary16 (to
   nearest, ties to even; overflow and subnormals), folded and evaluated as
   clang's: the printed bits are compared */
extern "C" int printf(const char *, ...);
typedef _Float16 H;
static void show(const char *n, H v) { printf("%s %04x\n", n, __builtin_bit_cast(unsigned short, v)); }
template <class T> constexpr T C(double x) { return static_cast<T>(x); }
#define S(n, e) { constexpr H k##n = e; show(#n, k##n); static const H s##n = e; show("s" #n, s##n); }
static_assert(static_cast<H>(1.1) != 1.1f, "rounded");
static_assert(static_cast<H>(2049) == 2048, "ties to even");
static_assert(static_cast<H>(2051) == 2052, "ties to even");

int main()
{
    S(a, static_cast<H>(1.1)) S(b, static_cast<H>(2049)) S(c, static_cast<H>(2051))
    S(d, static_cast<H>(70000.0)) S(e, static_cast<H>(65519.9)) S(f, static_cast<H>(65520.0))
    S(g, static_cast<H>(5.9604644775390625e-8)) S(h, static_cast<H>(2.98023223876953125e-8))
    S(i, static_cast<H>(8.94069671630859375e-8)) S(j, static_cast<H>(-6.1e-5))
    S(k, static_cast<H>(1.1f) * static_cast<H>(3)) S(l, static_cast<H>(1) / static_cast<H>(3))
    S(m, static_cast<H>(2048) + static_cast<H>(1)) S(n, static_cast<H>(65504) * static_cast<H>(2))
    S(o, -static_cast<H>(0.1)) S(p, C<H>(1.0)) S(q, C<H>(0.1)) S(r, 1.1f16) S(s, 0.1F16)
    S(t, static_cast<H>(-0.0)) S(u, static_cast<H>(100) - static_cast<H>(0.007))
    constexpr float back = static_cast<float>(static_cast<H>(0.1)); printf("back %a\n", back);
    constexpr int ib = static_cast<int>(static_cast<H>(2049)); printf("ib %d\n", ib);
    constexpr bool eq = static_cast<H>(1.1) == 1.1f; printf("eq %d\n", eq);
}
