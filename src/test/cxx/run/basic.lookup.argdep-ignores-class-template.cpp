/* [basic.lookup.argdep]/4.2: argument-dependent lookup ignores all names but
   functions' and function templates': a class or alias template of an
   associated namespace does not stop a call of that name (doctest's
   hash(g_cs->subcaseStack, n), beside std::hash) */
extern "C" int printf(const char *, ...);

namespace lib
{
template <class T> struct hash {
    T value;
};
template <class T> using other = T *;
template <class T> struct holder {
    T value;
};
} // namespace lib

namespace app
{
struct sig {
    int n;
};

int hash(const lib::holder<sig> &h, int count)
{
    return h.value.n * 10 + count;
}

int other(const lib::holder<sig> &h)
{
    return h.value.n + 1;
}

int run()
{
    lib::holder<sig> h{{4}};
    return hash(h, 2) * 100 + other(h);
}
} // namespace app

int main()
{
    lib::holder<app::sig> h{{7}};
    printf("%d %d %d\n", app::run(), hash(h, 3), other(h));
}
