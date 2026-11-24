/* [temp.param]/1, [temp.param]/4: the type of a template parameter, and an
   explicit-specifier, naming a template template-parameter that comes
   before it in the same parameter list */
template <bool B, class T = void>
struct enable {
};
template <class T>
struct enable<true, T> {
    typedef T type;
};
template <class T>
struct always {
    static const bool value = true;
};
template <class T>
struct never {
    static const bool value = false;
};
template <class... T>
struct all {
    static const bool value = true;
};
template <class... T>
struct holder {
    int which;
    holder() : which(1) {}
    template <template <class> class D = always,
              typename enable<D<int>::value, int>::type = 0>
    holder(long) : which(4) {}
    template <template <class> class A = never, template <class> class D = always,
              typename enable<!A<int>::value && D<int>::value, int>::type = 0>
    holder(short) : which(5) {}
    template <template <class> class D = always,
              typename enable<all<D<T>...>::value, int>::type = 0>
    holder(char, char) : which(6) {}
    /* only declared: the parameter lists are what is analysed */
    template <template <class> class D = always, D<int> *P = nullptr>
    holder(int, int) : which(2) {}
    template <template <class> class D = always>
    explicit(!all<D<T>...>::value) holder(char) : which(3) {}
};
int main()
{
    holder<int, long> a;
    holder<int> b;
    holder<int> c(5L);
    holder<int> d(static_cast<short>(1));
    holder<int, long> e('a', 'b');
    if (e.which != 6)
        return 2;
    if (a.which != 1 || b.which != 1 || c.which != 4 || d.which != 5)
        return 1;
    return 0;
}
