/* __bf16 and _Float16 constants through __builtin_bit_cast: evaluated,
   folded and lowered bit for bit as clang's (Highway's bfloat16_t) */
extern "C" int printf(const char *, ...);
typedef __bf16 B;
typedef _Float16 H;
static void show(const char *n, B v) { printf("%s %04x\n", n, __builtin_bit_cast(unsigned short, v)); }
static void showh(const char *n, H v) { printf("%s %04x\n", n, __builtin_bit_cast(unsigned short, v)); }
constexpr B from(unsigned short bits) { return __builtin_bit_cast(B, bits); }
constexpr unsigned short bits(B v) { return __builtin_bit_cast(unsigned short, v); }
struct W { B native; constexpr W(B n) : native(n) {} static constexpr W make(unsigned short b) { return W(__builtin_bit_cast(B, b)); } };
template <unsigned N> struct V { static constexpr unsigned n = N; };

int main()
{
    { constexpr B k = from(0x3f80); show("one", k); static const B s = from(0xc000); show("neg", s); }
    { constexpr B nan = from(0x7fc1); show("nan", nan); }
    { constexpr B inf = from(0xff80); show("inf", inf); constexpr B sub = from(0x0001); show("sub", sub); }
    { constexpr B negzero = from(0x8000); show("negzero", negzero); }
    printf("rt %04x %u\n", bits(from(0x1234)), V<bits(from(0x4321))>::n);
    { constexpr W w = W::make(0x4000); show("w", w.native); static constexpr W sw = W::make(0x4040); show("sw", sw.native); }
    { constexpr H h = __builtin_bit_cast(H, static_cast<unsigned short>(0x3c01)); showh("h", h);
      constexpr unsigned short hb = __builtin_bit_cast(unsigned short, static_cast<H>(1.5)); printf("hb %04x %u\n", hb, V<hb>::n); }
}
