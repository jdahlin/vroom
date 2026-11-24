// [dcl.fct.def.general]/8 __func__, and clang's __FUNCTION__ (same name) and
// __PRETTY_FUNCTION__ (the signature; a member of a class template's
// specialization and a lambda differ from clang's and are not compared).
extern "C" int strcmp(const char *, const char *);

struct B {
    int m;
};

namespace ns
{
struct S {
    S();
    ~S();
    int g(int) const;
    static const char *h();
    operator int();
    int operator+(int);
    const char *ctor_name, *dtor_target;
};
const char *dtor_seen;
S::S() { ctor_name = __func__; }
S::~S() { dtor_seen = __func__; }
int S::g(int) const
{
    return strcmp(__func__, "g") + strcmp(__FUNCTION__, "g") +
           strcmp(__PRETTY_FUNCTION__, "int ns::S::g(int) const");
}
const char *S::h() { return __func__; }
S::operator int() { return strcmp(__func__, "operator int"); }
int S::operator+(int) { return strcmp(__func__, "operator+"); }

template <class T> const char *tf(T) { return __func__; }
template <class T> const char *tp(T) { return __PRETTY_FUNCTION__; }
const char *vp(int (*)(int), int B::*, char (&)[2], const int *const *)
{
    return __PRETTY_FUNCTION__;
}
template <class T> struct TS {
    const char *m() { return __func__; }
};
} // namespace ns

static int plain(int) { return strcmp(__func__, "plain"); }

int main()
{
    int bad = 0;
    {
        ns::S s;
        bad += strcmp(s.ctor_name, "S");
        bad += s.g(1);
        bad += strcmp(ns::S::h(), "h");
        bad += (int)s;
        bad += s.operator+(1);
    }
    bad += strcmp(ns::dtor_seen, "~S");
    bad += strcmp(ns::tf(1), "tf") + strcmp(ns::tf<char>(0), "tf");
    bad += strcmp(ns::TS<int>().m(), "m");
    bad += plain(0);
    bad += strcmp(ns::tp(1), "const char *ns::tp(T) [T = int]");
    bad += strcmp(ns::tp<long>(1),
                  "const char *ns::tp(T) [T = long]");
    bad += strcmp(ns::vp(0, 0, *(char(*)[2])0, 0),
                  "const char *ns::vp(int (*)(int), int B::*, char (&)[2], "
                  "const int *const *)");
    bad += strcmp(ns::S::h(), "h");
    auto l = [](int) { return strcmp(__func__, "operator()"); };
    bad += l(1);
    bad += strcmp(__func__, "main");
    bad += strcmp(__PRETTY_FUNCTION__, "int main()");
    static_assert(sizeof(__func__) == 5, "main + NUL");
    const char(&r)[5] = __func__;
    bad += r[0] != 'm';
    return bad;
}
