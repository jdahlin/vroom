/* [over.ics.rank]/3.2.6, [basic.type.qualifier]/6: references to arrays
   that differ in their elements' cv-qualifiers. */
extern "C" int printf(const char *, ...);
template <class T> int f(T (&)[4]) { return 1; }
template <class T> int f(const T (&)[4]) { return 2; }
int g(const int (&)[2]) { return 3; }
int g(int (&)[2]) { return 4; }
int h(int (&)[2][2]) { return 5; }
int h(const int (&)[2][2]) { return 6; }
template <class T, unsigned long N> struct Sp {
    T *p;
    constexpr Sp(T (&a)[N]) : p(a) {}
};
template <class T, unsigned long N> Sp(T (&)[N]) -> Sp<T, N>;
template <class T, unsigned long N> Sp(const T (&)[N]) -> Sp<const T, N>;
struct C { unsigned short offsets[4]; };
int main()
{
    int a[4] = {}, c[2] = {}, m[2][2] = {};
    const int b[4] = {}, d[2] = {}, n[2][2] = {};
    printf("%d %d %d %d %d %d\n", f(a), f(b), g(c), g(d), h(m), h(n));
    C x{};
    auto s = Sp(x.offsets);
    s.p[1] = 7;
    printf("%d\n", x.offsets[1]);
}
