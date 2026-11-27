/* [class.temporary]/4: the temporaries a call's arguments are bound to
   live to the end of the full-expression: an inlined callee must not give
   their stack slots away while a returned reference names one. */
typedef unsigned short u16;
typedef decltype(sizeof 0) size;
template <class T, size N> struct array {
    T e[N];
    T &operator[](size i) { return e[i]; }
    const T &operator[](size i) const { return e[i]; }
};
struct less {
    bool operator()(const u16 &a, const u16 &b) const { return a < b; }
};
template <class C> const u16 &max3(const u16 &a, const u16 &b, C c)
{
    return c(a, b) ? b : a;
}
template <class C> const u16 &min3(const u16 &a, const u16 &b, C c)
{
    return c(b, a) ? b : a;
}
inline const u16 &larger(const u16 &a, const u16 &b) { return max3(a, b, less()); }
inline const u16 &smaller(const u16 &a, const u16 &b) { return min3(a, b, less()); }
struct syntax {
    unsigned char has : 1, map : 1, rest : 3;
    u16 white;
    array<u16, 4> other;
};
namespace
{
template <typename T, typename U, typename V> T clamp(T x, U a, V b)
{
    return smaller(larger(x, static_cast<T>(a)), static_cast<T>(b));
}
float scaled(u16 v, u16 low, u16 high, u16 offset, float scale)
{
    return (static_cast<int>(clamp(v, low, high)) - offset) / scale;
}
} // namespace
struct images {
    float base;
    array<float, 4> headroom;
};
struct metadata {
    float white;
    images map;
    bool parse(const syntax &s);
};
bool metadata::parse(const syntax &s)
{
    if (s.has == 1)
        white = scaled(s.white, 1u, 50000u, 0u, 5.f);
    if (s.map == 0)
        return true;
    images &m = map = images();
    for (int a = 0; a < 2; ++a)
        m.headroom[a] = scaled(s.other[a], 0u, 60000u, 0u, 10000.f);
    return true;
}
int main()
{
    syntax s{};
    s.has = 1;
    volatile int zero = 0;
    s.white = zero;
    metadata m{};
    m.parse(s);
    return m.white != 0.2f;
}
