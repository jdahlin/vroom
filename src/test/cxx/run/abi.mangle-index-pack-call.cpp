/* [abi:expressions] a decltype return type of a call wrapper's member
   (libc++'s bind_front, bind_back): a class's index pack expanded element by
   element, calls whose operands do not depend named as resolved, a member of
   *this, a functional cast to a template-id, casts to reference types */
namespace n {
template <unsigned long... I> struct iseq {};
template <class T> T &&fwd(T &t) { return static_cast<T &&>(t); }
template <unsigned long I> int get(const int *p) { return p[I]; }
template <unsigned long I, class T> const T &at(const T (&p)[2]) { return p[I]; }
template <class F, class... A> auto inv(F f, A... a) -> decltype(f(a...))
{
    return f(a...);
}

template <class S> struct back;
template <unsigned long... Ip> struct back<iseq<Ip...>> {
    template <class F, class B, class... A>
    auto operator()(F &&f, B &&b, A &&...a) const
        -> decltype(n::inv(n::fwd<F>(f), n::fwd<A>(a)...,
                           n::get<Ip>(n::fwd<B>(b))...))
    {
        return n::inv(n::fwd<F>(f), n::fwd<A>(a)...,
                      n::get<Ip>(n::fwd<B>(b))...);
    }
};

template <class Op, class S> struct front;
template <class Op, unsigned long... Ip> struct front<Op, iseq<Ip...>> {
    int bound[2];
    template <class... A>
    auto operator()(A &&...a) const
        -> decltype(Op()(n::at<Ip>(bound)..., static_cast<A &&>(a)...))
    {
        return Op()(n::at<Ip>(bound)..., static_cast<A &&>(a)...);
    }
};

template <class T> struct holder {
    T v;
    holder(T a) : v(a) {}
};
template <class F, class T> struct pair {
    F f;
    T t;
    pair(F a, T b) : f(a), t(b) {}
};
template <class... A> int count(A &&...) { return sizeof...(A); }
template <class F, class... A>
auto make(F &&f, A &&...a)
    -> decltype(pair<F, holder<int>>(n::fwd<F>(f),
                                     n::count(n::fwd<A>(a)...)))
{
    return pair<F, holder<int>>(n::fwd<F>(f), n::count(n::fwd<A>(a)...));
}

template <class A> auto lref(A &a) -> decltype(static_cast<A &>(a))
{
    return a;
}
template <class A> auto cref(A &a) -> decltype((const A &)a) { return a; }
}

struct add {
    int operator()(int a, int b, int c) const { return a * 100 + b * 10 + c; }
};
int add3(int a, int b, int c) { return a * 100 + b * 10 + c; }

int main()
{
    n::back<n::iseq<0, 1>> b;
    int bound[2] = {2, 3};
    int *p = bound;
    if (b(add3, p, 1) != 123)
        return 1;
    n::front<add, n::iseq<0, 1>> f{{1, 2}};
    if (f(3) != 123)
        return 2;
    int k = 1;
    if (n::make(k, 2, 3).t.v != 2)
        return 3;
    if (n::lref(k) + n::cref(k) != 2)
        return 4;
    return 0;
}
