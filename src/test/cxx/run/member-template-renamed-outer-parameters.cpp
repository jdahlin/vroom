// [temp.mem]/1, [temp.param]: the out-of-class definition of a member
// template of a class template names the class's parameters as it likes
// (W for U); they correspond by position.
template <class T, class U = int>
struct V {
    using R = T;
    T x;
    template <class... A> typename V<T, U>::R put(A &&...a);
    template <class A> U other(A a);
};

template <class T, class W>
template <class... A>
typename V<T, W>::R V<T, W>::put(A &&...a)
{
    return x + static_cast<T>(sizeof...(A));
}

template <class T, class W>
template <class A>
W V<T, W>::other(A a)
{
    return static_cast<short>(a) + 1;
}

int main()
{
    V<int> v{1};
    if (v.put(1, 2) != 3)
        return 1;
    V<long, short> w{5};
    if (w.other(2.0) != 3)
        return 2;
    return 0;
}
