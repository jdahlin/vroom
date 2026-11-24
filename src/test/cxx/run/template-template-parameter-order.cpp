/* [temp.param]/1, [temp.param]/4: the type of a template parameter, and an
   explicit-specifier, naming a template template-parameter that comes
   before it in the same parameter list */
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
    if (a.which != 1 || b.which != 1)
        return 1;
    return 0;
}
