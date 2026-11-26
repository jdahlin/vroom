/* [ext:clang_type_traits] __is_pod of a class with a constructor template:
   the template is no default constructor, so without X() = default the
   class is no POD; with it the other members decide. */

struct a {
    template <class T> a(T);
};
struct c {
    template <class T = int> c();
};
struct d {
    template <class... T> d(T...);
};
struct e {
    e() = default;
    template <class... T> e(T...);
};
struct f {
    int x;
    f() = default;
    template <class T> f(const T &);
};
struct g {
    template <class T> g(T);
    g(const g &);
};
struct l {
    l() = default;
    template <class T> l(T);

  private:
    int first;

  public:
    int second;
};
struct m {
    m() = default;
    template <class T> m(T);
    int value = 1;
};

template <bool B> struct bit {
    static constexpr int value = B;
};

int main()
{
    int got = bit<__is_pod(a)>::value | bit<__is_pod(c)>::value << 1 |
              bit<__is_pod(d)>::value << 2 | bit<__is_pod(e)>::value << 3 |
              bit<__is_pod(f)>::value << 4 | bit<__is_pod(g)>::value << 5 |
              bit<__is_pod(l)>::value << 6 | bit<__is_pod(m)>::value << 7;
    return got == (1 << 3 | 1 << 4) ? 0 : 1;
}
