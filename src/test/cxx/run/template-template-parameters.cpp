/* Template template-parameters and non-type template-parameters of
   enumeration, bool, char, pointer and reference types; unnamed
   template-parameters ([temp.param], [temp.arg.template],
   [temp.arg.nontype], [temp.deduct.type]/8's TT<T>). */
#include <stdio.h>

template <class T> struct box {
    T v;
    T get() const { return v; }
};
namespace N {
template <class T> struct bag {
    T v;
    T get() const { return v * 2; }
};
} // namespace N

template <template <class> class C> struct holder {
    C<int> a;
    C<long> b;
    long sum() const { return a.get() + b.get(); }
};
template <template <class> class C = box> struct with_default {
    C<short> s;
};
template <template <class> class C, class T> T twice(C<T> c)
{
    return c.get() + c.get();
}
template <template <class> class C> long total(holder<C> x) { return x.sum(); }

enum colour { red, green = 7 };
enum class level : char { low, high = 3 };
template <colour c> struct paint {
    static int value() { return c; }
};
template <level l> struct rank {
    static int get() { return (int)l + 1; }
};
template <bool b, char ch> struct flags {
    static int get() { return b ? ch : -ch; }
};

int global = 11;
int table[4] = {1, 2, 3, 4};
int seven() { return 7; }
template <int *P> struct points {
    int n;
};
template <int &R> struct refers {
    int n;
};
template <int (*F)()> struct calls {
    int n;
};
template <int *P = nullptr> struct maybe {
    int n;
};
template <class, int = 3> struct unnamed {
    int n;
};
template <int *P> int same(points<P> a, points<P> b) { return a.n + b.n; }

int main()
{
    int failed = 0;
    holder<box> h = {{2}, {3}};
    if (h.sum() != 5)
        failed |= 1;
    holder<N::bag> g = {{2}, {3}};
    if (g.sum() != 10)
        failed |= 2;
    with_default<> d = {{4}};
    if (d.s.get() != 4 || sizeof(d.s) != sizeof(short))
        failed |= 4;
    box<long> bl = {21};
    N::bag<int> bi = {5};
    if (twice(bl) != 42 || twice(bi) != 20)
        failed |= 8;
    if (total(h) != 5 || total(g) != 10)
        failed |= 16;
    if (paint<green>::value() != 7 || paint<red>::value() != 0)
        failed |= 32;
    if (rank<level::high>::get() != 4 || rank<level::low>::get() != 1)
        failed |= 64;
    if (flags<true, 'a'>::get() != 'a' || flags<false, 2>::get() != -2)
        failed |= 128;
    points<&global> p = {1};
    points<table> q = {2};
    refers<global> r = {3};
    calls<seven> s = {4};
    calls<&seven> t = {5};
    maybe<> m = {6};
    unnamed<int> u = {7};
    if (p.n + q.n + r.n + s.n + t.n + m.n + u.n != 28)
        failed |= 256;
    if (same(p, points<&global>{9}) != 10)
        failed |= 512;
    printf("%d\n", failed);
    return failed;
}
