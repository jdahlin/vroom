/* [dcl.mptr]/1, [temp.dep.type]/9, [temp.deduct.type]/8: a pointer to
   member of a class that is a template parameter, in a member declaration
   and in a partial specialization's argument (libc++'s __weak_result_type) */
template <class T> struct kind { static constexpr int v = 0; };
template <class R, class C> struct kind<R (C::*)()> { static constexpr int v = 1; };
template <class R, class C> struct kind<R C::*> { static constexpr int v = 3; };
template <class R, class C, class A>
struct kind<R (C::*)(A)> { static constexpr int v = 4; };
struct S {
    int f = 5;
    int m() { return f; }
    long g(char c) { return c; }
};
template <class R, class C> struct Holder {
    typedef R (C::*Fn)();
    Fn fn;
    R C::*data;
    R call(C &c) { return (c.*fn)(); }
    R get(C &c) { return c.*data; }
};
static_assert(kind<int>::v == 0);
static_assert(kind<int (S::*)()>::v == 1);
static_assert(kind<int S::*>::v == 3);
static_assert(kind<long (S::*)(char)>::v == 4);
int main()
{
    S s;
    Holder<int, S> h{&S::m, &S::f};
    return h.call(s) + h.get(s) - 10;
}
