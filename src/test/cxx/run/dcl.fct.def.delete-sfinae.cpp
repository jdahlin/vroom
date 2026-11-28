/* [dcl.fct.def.delete]/2, [expr.prim.req.simple]: an expression that selects
   a deleted function, template or not, does not satisfy a requirement
   (libc++'s poison-pill swap). */
namespace lib {
template <class T> void swap(T &, T &) = delete;
template <class T>
concept has_swap = requires(T &a, T &b) { swap(a, b); };
struct fn {
    template <class T>
        requires has_swap<T>
    int operator()(T &a, T &b) const
    {
        swap(a, b);
        return 1;
    }
    template <class T>
        requires(!has_swap<T>)
    int operator()(T &a, T &b) const
    {
        T t = a;
        a = b;
        b = t;
        return 2;
    }
};
inline constexpr fn exchange{};
} // namespace lib

void gone(int &) = delete;
template <class T>
concept goes = requires(T &a) { gone(a); };
static_assert(!goes<int>);

struct P {
    int a;
};
struct Q {
    int a;
};
void swap(Q &a, Q &b) { int t = a.a; a.a = b.a + 10; b.a = t; }

static_assert(!lib::has_swap<int>);
static_assert(!lib::has_swap<P>);
static_assert(lib::has_swap<Q>);

int main()
{
    P x{1}, y{2};
    if (lib::exchange(x, y) != 2 || x.a != 2 || y.a != 1)
        return 1;
    Q u{1}, v{2};
    if (lib::exchange(u, v) != 1 || u.a != 12 || v.a != 1)
        return 2;
    return 0;
}
