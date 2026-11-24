/* [class.friend]/6, [class.access]/6: the body of a friend function template
   defined in a class template reads the private member of another
   specialization that befriends the whole template (libc++'s __wrap_iter) */
template <class I> struct W {
private:
    I i_;

public:
    W(I i = 0) : i_(i) {}
    template <class J> friend class W;
    template <class J>
    friend auto operator-(const W &x, const W<J> &y) noexcept
        -> decltype(x.i_ - y.i_)
    {
        return x.i_ - y.i_;
    }
};
template <class T> struct Tr {
    typedef const T *cp;
};
template <class T> struct S {
    typedef W<typename Tr<T>::cp> ci;
    long f(ci a, ci b);
};
template <class T> long S<T>::f(ci a, ci b)
{
    return static_cast<long>(a - b);
}
int main()
{
    int v[4] = {0, 1, 2, 3};
    S<int> s;
    W<const int *> a(v + 3), b(v + 1);
    W<int *> c(v + 2);
    if (s.f(a, b) != 2)
        return 1;
    if (a - c != 1)
        return 2;
    return 0;
}
