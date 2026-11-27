/* [dcl.type.auto.deduct]/4: auto from a braced list of classes and of
   string literals (std::initializer_list), their lifetime the variable's */
namespace std {
template <class E> class initializer_list {
    const E *first;
    decltype(sizeof 0) count;

  public:
    constexpr initializer_list() : first(nullptr), count(0) {}
    constexpr const E *begin() const { return first; }
    constexpr const E *end() const { return first + count; }
    constexpr decltype(sizeof 0) size() const { return count; }
};
}
extern "C" int printf(const char *, ...);
struct T {
    double a;
    int n;
    int v[3];
    T(double x, std::initializer_list<int> l) : a(x), n((int)l.size())
    {
        int i = 0;
        for (int e : l)
            v[i++] = e;
        printf("T %d\n", n);
    }
    T(const T &o) : a(o.a), n(o.n) { printf("copy\n"); }
    ~T() { printf("~T %d\n", n); }
};
static int twice(int x) { return 2 * x; }
int main()
{
    {
        auto tests = {T{1.5, {1, 2, 3}}, T{2.5, {4, 5}}};
        for (auto &t : tests)
            printf("%d %d %d\n", (int)(t.a * 2), t.n, t.v[0]);
    }
    auto exts = {"png", "PNG", "gif"};
    for (auto e : exts)
        printf("%s\n", e);
    auto fns = {twice, twice};
    for (auto f : fns)
        printf("%d\n", f(4));
}
