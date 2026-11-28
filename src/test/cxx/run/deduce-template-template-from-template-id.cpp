/* cxx-run-flags: -std=c++20 */
/* [temp.deduct.type]/8: TT is deduced from a parameter TT<int>, whose
   arguments name no template-parameter: in a call, in constructor
   templates, in a member template of a class template and in a
   deduction-guide. */
template <class T> struct A {
    int value;
};

template <class T> struct Other {
    int value;
};

template <template <class> class TT> struct Which;
template <> struct Which<A> {
    static constexpr int id = 1;
};
template <> struct Which<Other> {
    static constexpr int id = 2;
};

template <template <class> class TT> int call(TT<int> x)
{
    return Which<TT>::id * 100 + x.value;
}

/* an explicitly specified TT takes no part in deduction: the argument is
   converted ([temp.arg.explicit]/7) */
struct ToA {
    operator A<int>() const
    {
        return A<int>{9};
    }
};

struct Constructed {
    int got;
    template <template <class> class TT>
    Constructed(TT<int> x) : got(call(x))
    {
    }
};

template <class T> struct Holder {
    int got;
    template <template <class> class TT>
    Holder(TT<T> x) : got(call(x))
    {
    }
    template <template <class> class TT> static int take(TT<T> x)
    {
        return call(x);
    }
};

template <template <class> class TT> struct Guided {
    int got;
    template <class T> Guided(TT<T> x) : got(x.value)
    {
    }
    int which() const
    {
        return Which<TT>::id * 100 + got;
    }
};
template <template <class> class TT> Guided(TT<int>) -> Guided<TT>;

/* [temp.names]/7, [temp.deduct]/8: TT<U> with U explicitly specified must
   satisfy the constraints of TT's own parameters, or deduction fails */
template <class T> concept Small = sizeof(T) == 1;

template <class U, template <Small> class TT> int small_argument(TT<U>)
{
    return 3;
}
/* the other candidate, behind a user-defined conversion */
struct Any {
    template <class T> Any(T)
    {
    }
};
template <class U> int small_argument(Any)
{
    return 4;
}

int main()
{
    if (call(A<int>{3}) != 103 || call(Other<int>{4}) != 204)
        return 1;
    if (call<A>(ToA{}) != 109)
        return 2;
    Constructed c(Other<int>{5});
    if (c.got != 205)
        return 3;
    Holder h(A<int>{6});
    if (h.got != 106 || Holder<int>::take(Other<int>{7}) != 207)
        return 4;
    auto g = Guided(Other<int>{8});
    if (g.which() != 208)
        return 5;
    if (small_argument<int>(A<int>{0}) != 4)
        return 6;
    return 0;
}
