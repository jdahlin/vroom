/* [dcl.type.auto.deduct]/4: a placeholder deduced from a bit-field takes
   its declared type. */
extern "C" int printf(const char *, ...);
enum E : unsigned { e0, e1, e5 = 5 };
struct S { int a : 4; unsigned b : 7; E c : 4; long long d : 40; bool f : 1; };
template <class T, class U> constexpr bool same = __is_same(T, U);
auto get(S &s) { return s.d; }
template <class T> auto tget(T &s) { auto v = s.a; return v; }
int main() {
    S s{-3, 100, e5, 123456789012LL, true};
    auto a = s.a; auto b = s.b; auto c = s.c; auto d = get(s);
    const auto &r = s.b; auto &&rr = s.a + 0; decltype(auto) q = s.b;
    static_assert(same<decltype(a), int>); static_assert(same<decltype(b), unsigned>);
    static_assert(same<decltype(c), E>); static_assert(same<decltype(d), long long>);
    static_assert(same<decltype(q), unsigned>);
    auto l = [x = s.a, y = s.c] { return x * 100 + y; };
    s.a = 2; s.b = 1;
    printf("%d %u %u %lld %u %d %u %d %d %d\n", a, b, (unsigned)c, d, r, rr, q, l(), tget(s), (int)sizeof(a + s.a));
    if (auto n = s.b) printf("%u\n", n);
    auto *p = new auto(s.d); printf("%lld\n", *p);
}
