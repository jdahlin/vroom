/* [temp.alias]/2, [expr.prim.lambda.closure]/2: an alias template whose
   type is a closure type passed in as an argument names that closure; the
   closure stays in the function whose lambda-expression created it, and
   is not moved into the function that substituted the alias (libc++'s
   __atomic_wait_unless names its caller's lambda as __decay_t<_Poll>; the
   closure's scope pointed back at the specialization that had it as a
   template argument, and mangling that specialization recursed until the
   stack ran out) */
template <class T> struct decay {
    using type = T;
};
template <class T> using decay_t = typename decay<T>::type;
template <class P> struct holder {
    P p;
};
template <class F> int poll(F f)
{
    return f();
}
template <class P> int wait_unless(P &&p)
{
    holder<decay_t<P>> h = {p};
    return poll([&]() { return h.p(1); });
}
template <class T> int wait(T v)
{
    return wait_unless([&](T c) { return c != v; });
}
int main()
{
    if (wait(1))
        return 1;
    if (!wait(2))
        return 2;
    return 0;
}
