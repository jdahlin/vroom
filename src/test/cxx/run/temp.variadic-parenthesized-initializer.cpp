/* [temp.variadic]/5, [dcl.init]/17.6.2: a pack expansion among the
   arguments of a direct-initialization of a class */
struct T {
    int v;
    T(int a) : v(a) {}
    T(int a, int b) : v(a + b) {}
    T(int a, int b, int c) : v(a + b + c) {}
};
template <class... A> int f(A... a)
{
    T x(100, a...);
    return x.v;
}
template <class... A> int g(A... a)
{
    T x(a..., 200);
    return x.v;
}
template <class V> struct D {
    template <class... A> int h(int pos, A &&...a)
    {
        if (pos) {
            T x(1, static_cast<A &&>(a)...);
            return x.v;
        }
        return 0;
    }
};
int main() { D<int> d; return f() + f(1) + g(1, 2) + d.h(1, 2) - (100 + 101 + 203 + 3); }
