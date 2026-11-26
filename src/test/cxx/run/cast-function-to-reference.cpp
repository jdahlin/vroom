/* [expr.static.cast]: a function cast to a reference to its type, as
   std::forward does for a function argument */
template <class T> struct Id {
    using type = T;
};
template <class T> T &&forward(typename Id<T>::type &t)
{
    return static_cast<T &&>(t);
}
static int twice(int v) { return 2 * v; }
template <class F> int call(F &&f) { return forward<F>(f)(21); }

int main()
{
    int (&r)(int) = static_cast<int (&)(int)>(twice);
    return call(twice) + r(0) != 42 ||
           static_cast<int (&&)(int)>(twice)(1) != 2;
}
