/* [temp.expl.spec]/1: explicit specializations of a constructor template,
   defined with a mem-initializer-list (delegating, and of the members),
   with and without a decl-specifier */
struct P {
    int x = 0;
    int y = 0;
    P(int a, int b) : x(a), y(b) {}
    template <class T> P(const T &) : x(100), y(100) {}
};
struct pair_source {
    int a, b;
};
template <> inline P::P(const pair_source &s) : P(s.a, s.b) {}
template <> P::P(const long &v) : x(static_cast<int>(v)), y(7) {}
template <> P::P(const char &c)
{
    x = c;
    y = 1;
}
int main()
{
    pair_source s = {3, 4};
    long l = 5;
    char c = 6;
    double d = 1.0;
    P a(s), b(l), e(c), f(d);
    if (a.x != 3 || a.y != 4)
        return 1;
    if (b.x != 5 || b.y != 7)
        return 2;
    if (e.x != 6 || e.y != 1)
        return 3;
    if (f.x != 100 || f.y != 100)
        return 4;
    return 0;
}
