/* [temp.spec.partial.match]/2: a partial specialization whose argument is
   a decltype of its parameters ([temp.deduct.type]/5.3, a non-deduced
   context) matches when the deduced arguments substituted into it give
   the use's; a substitution failure is no match ([temp.deduct]/8) */
template <class T> T &&declval();
template <class, class, class> struct r {
    static constexpr int v = 0;
};
template <class T, class U>
struct r<T, U, decltype(declval<T>() + declval<U>(), void())> {
    static constexpr int v = 1;
};
template <class, class> struct s {
    static constexpr int v = 0;
};
template <class T> struct s<T, decltype(declval<T>())> {
    static constexpr int v = 1;
};
static_assert(r<int, int, void>::v == 1);
static_assert(r<int *, int, void>::v == 1);
static_assert(r<int *, int *, void>::v == 0);
static_assert(r<int, int, int>::v == 0);
static_assert(s<int, int &&>::v == 1);
static_assert(s<int, int>::v == 0);

int f(int x) { return r<int, long, void>::v * 10 + r<int *, int *, void>::v + x; }
int g() { return s<long, long &&>::v * 100 + s<long, long &>::v; }

int main() { return f(0) == 10 && g() == 100 ? 0 : 1; }
