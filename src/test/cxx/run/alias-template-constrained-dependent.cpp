/* [temp.alias]/2: a constrained alias template with a dependent argument
   (libc++'s range_difference_t<_Rp> in a member function template's
   return type) denotes a dependent type; [temp.names]/7's constraints are
   checked once the argument is known */
template <class T>
concept has_diff = requires { typename T::diff; };
template <class T>
using ptr_t = T *;
template <has_diff T>
using diff_t = ptr_t<typename T::diff>;
template <class T>
concept fallback = !has_diff<T>;

struct R {
    using diff = long;
};
struct S {
};

struct distance_fn {
    template <has_diff T>
    diff_t<T> operator()(T &, long *p) const { return p + 1; }
    template <fallback T>
    int operator()(T &, int x) const { return x; }
};

int main()
{
    long a[3] = {1, 2, 42};
    R r;
    S s;
    distance_fn d;
    return (int)*d(r, &a[1]) + d(s, 0) - 42;
}
