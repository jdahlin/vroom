/* [abi:expressions] rQ, a requires-expression with local parameters in a
   requires-clause (libc++'s reverse_iterator::operator->), and an
   operator-function-id named in it (on pt) */
/* cxx-run-flags: -std=c++20 */
template <class T> inline constexpr bool is_pointer_v = false;
template <class T> inline constexpr bool is_pointer_v<T *> = true;

template <class I> struct reverse {
    I current;
    constexpr I operator->() const
        requires is_pointer_v<I> || requires(const I i) { i.operator->(); }
    {
        return current;
    }
    constexpr I get() const
        requires requires(I i, int n) {
            i.m(n);
            *i;
        }
    {
        return current;
    }
};

template <class T>
    requires requires(const T a, T &b) { a.x + b.x; }
int sum(T t)
{
    return t.x;
}

/* (a trailing clause's parameters are fp, a template-head's fL0p) */
template <class T>
int trailing(T t)
    requires requires(const T a, T &b) { a.x + b.x; }
{
    return t.x;
}
template <class T> struct M {
    template <class U>
        requires requires(U u) { u.x; }
    int head(U u)
    {
        return u.x;
    }
    template <class U>
    int tail(U u)
        requires requires(U v) { v.x; }
    {
        return u.x;
    }
};

struct S {
    int x;
    void m(int) {}
    S &operator*() { return *this; }
};

int main()
{
    S s{3};
    reverse<S *> r{&s};
    reverse<S> q{s};
    M<int> m;
    return r.operator->()->x + q.get().x + sum(s) + trailing(s) + m.head(s) +
           m.tail(s) - 18;
}
