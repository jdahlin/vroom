/* [temp.deduct.call]/4.3, [temp.over]/1: an argument of a specialization not
   yet complete, not derived from the parameter's template: deduction fails
   and the other candidate is called. */
extern "C" int printf(const char *, ...);

template <class T> struct span {};
template <class T> struct other {
    int v = 3;
};
template <class T> struct derived : span<T> {};

template <class U> int take(const span<U> &) { return 1; }
template <class U> int take(U &&) { return 2; }

template <class T> other<T> &make_other();
template <class T> derived<T> &make_derived();

template <class T>
concept narrow = requires { sizeof(take(make_other<T>())); };

int main()
{
    static_assert(narrow<int>);
    static other<char> o;
    static derived<long> d;
    printf("%d %d\n", take(o), take(static_cast<const derived<long> &>(d)));
    return 0;
}
