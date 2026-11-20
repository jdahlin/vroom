/* [temp.arg.template]/1, /3: an alias template as the argument of a
   template template-parameter (libc++'s __detected_or_t<_Default,
   __iterator_category, ...>), and P0522R0's matching: defaults of the
   argument's parameters, packs on either side */
template <class T> using Ptr = T *;
template <class T, class U = int> struct Pair {
    T a;
    U b;
};
template <template <class> class F, class T> struct One {
    F<T> v;
};
template <template <class...> class F, class... T> struct Many {
    F<T...> v;
};
template <template <class, class> class F, class T> struct Two {
    F<T, char> v;
};
template <class T> struct Box {
    T v;
};
template <template <class> class F> struct Names {
    static int size() { return sizeof(F<long>); }
};
template <class T, class V = void> struct Detect {
    using type = int;
};
template <template <class> class Op, class T> using Apply = Op<T>;

One<Ptr, int> a1;
Many<Ptr, int> a2;
Many<Pair, int, long> a3;
Two<Pair, int> a4;
One<Pair, short> a5;
One<Box, long> a6;
Apply<Ptr, char> a7;

static_assert(sizeof(a1.v) == sizeof(void *), "");
static_assert(sizeof(a3.v) == 16, "");
static_assert(sizeof(a4.v) == 8, "");
static_assert(sizeof(a5.v) == 8, "");
static_assert(sizeof(a7) == sizeof(void *), "");

int main()
{
    if (Names<Ptr>::size() != sizeof(void *) || Names<Box>::size() != 8)
        return 1;
    int x = 3;
    a1.v = &x;
    a3.v.b = 5;
    if (*a1.v == 3 && a3.v.b == 5)
        return 0;
    return 1;
}
