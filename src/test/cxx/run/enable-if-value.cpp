/* libc++'s SFINAE: a non-type template-parameter whose type depends on
   the parameters before it (enable_if_t<is_integral<I>::value, int> = 0,
   [temp.param]/4), analysed again with the arguments, and a
   value-dependent template argument (is_integral<I>::value,
   [temp.dep.constexpr]/2) in the template's own declaration.  The
   overload whose enable_if has no type is no candidate ([temp.deduct]/8).
   Symbols as clang 17 and later: Tn <type> before the argument of a
   parameter of dependent type, X sr Q<args> E name E for the value. */
extern "C" int printf(const char *, ...);

template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> {
    typedef T type;
};
template <bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;

template <class T> struct is_integral {
    static const bool value = false;
};
template <> struct is_integral<int> {
    static const bool value = true;
};
template <> struct is_integral<long> {
    static const bool value = true;
};

struct byte {
    unsigned char v;
};

/* libc++'s <cstddef> spelling */
template <class I, enable_if_t<is_integral<I>::value, int> = 0>
int shift(int x, I s)
{
    return x << s;
}

template <class T, typename enable_if<is_integral<T>::value, int>::type = 0>
int kind(T)
{
    return 1;
}
template <class T, typename enable_if<!is_integral<T>::value, int>::type = 0>
int kind(T)
{
    return 2;
}

/* a value-dependent argument of a class template in a parameter's type */
template <bool B> struct flag {
    static const int v = B;
};
template <class T> int flagged(T, flag<is_integral<T>::value> *f)
{
    return f->v;
}

/* the same value-dependent argument twice: one type, a substitution */
template <class T>
int twice(flag<is_integral<T>::value> *a, flag<is_integral<T>::value> *b)
{
    return a->v + b->v;
}

/* integral_constant's form, with a function template */
template <class T, T v> T constant(T x)
{
    return x + v;
}

int main()
{
    byte b = {1};
    flag<true> yes;
    flag<false> no;
    printf("%d %ld %d %d %d %d %d\n", shift(1, 3), (long)shift(1, 4L),
           kind(5), kind(2L), kind(b), flagged(1, &yes), flagged(b, &no));
    printf("%d %ld\n", twice<int>(&yes, &yes), constant<long, 5>(2));
    return shift(1, 3) == 8 && kind(b) == 2 ? 0 : 1;
}
