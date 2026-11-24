// [basic.lookup.argdep]/3: the functions found by argument-dependent lookup
// join those of unqualified lookup, a function template here, and overload
// resolution prefers the non-template function of an equal conversion.
namespace N {
struct X {
    int v;
    friend int pick(X &x, X &) { return x.v; }
};
int twice(X &x) { return x.v * 2; }
} // namespace N
template <class T> int pick(T &, T &) = delete;
template <class T> int twice(T &) { return 100; }
static int call_twice(N::X &x) { return twice(x); }
static int call_pick(N::X &x, N::X &y) { return pick(x, y); }
int main()
{
    N::X x{3};
    if (call_twice(x) != 6)
        return 1;
    if (call_pick(x, x) != 3)
        return 2;
    return 0;
}
