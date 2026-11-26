/* [temp.deduct.partial], [temp.deduct.type]/5.1: a parameter that is a
   non-deduced context (libc++'s string_view operator== overloads) deduces
   nothing in partial ordering; the template without it is more
   specialized */
extern "C" int printf(const char *, ...);
template <class T> struct id {
    using type = T;
};
template <class T> using id_t = typename id<T>::type;
template <class C> struct view {
    view(const C *) {}
};
template <class C> int eq(view<C>, view<C>) { return 1; }
template <class C, int = 2> int eq(id_t<view<C>>, view<C>) { return 2; }
template <class C, int = 3> int eq(view<C>, id_t<view<C>>) { return 3; }
template <class T> int one(T *, int) { return 4; }
template <class T> int one(T *, id_t<T>) { return 5; }
int main()
{
    view<char> v("a");
    int n = 0;
    printf("%d %d %d\n", eq(v, v), eq("b", v), eq(v, "c"));
    printf("%d\n", one((long *)&n, 1L));
    return 0;
}
