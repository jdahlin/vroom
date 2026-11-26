/* [dcl.init.ref]/5.4: a reference bound to a temporary converted from or
   to an enumeration */
enum W { zero, low = 5, high = 900 };
enum class S : short { s = 7 };
template <class T> constexpr const T &pin(const T &x, const T &lo, const T &hi)
{
    if (x < lo)
        return lo;
    if (hi < x)
        return hi;
    return x;
}
struct Style {
    int v;
    constexpr Style(int w) : v(pin<int>(w, low, high)) {}
};
int take(const long &r) { return static_cast<int>(r); }
int en(const W &w) { return w; }
int sc(S &&s) { return static_cast<int>(s); }
int main()
{
    static constexpr Style c(1);
    volatile int n = 1000;
    Style s(n);
    if (c.v != 5 || s.v != 900)
        return 1;
    W w = high;
    if (take(w) != 900 || take(low) != 5)
        return 2;
    if (en(low) != 5 || sc(S::s) != 7)
        return 3;
    const int &r = w;
    w = zero;
    return r != 900;
}
