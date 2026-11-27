/* [abi:mangle.template-arg]: a template template argument whose
   template-parameter-list is not its parameter's (Tt...E), and a pack
   expansion naming an enclosing class's pack among the arguments of a
   dependent qualifier (libc++'s _And<_IsDefault<_Tp>...>::value in tuple's
   default constructor) */
extern "C" int printf(const char *, ...);
template <class...> struct And {
    static const bool value = true;
};
template <class T> struct is_default {
    static const bool value = true;
};
template <bool, class T> struct enable_if {};
template <class T> struct enable_if<true, T> {
    typedef T type;
};
template <class... T> struct tup {
    template <template <class...> class Default = is_default,
              typename enable_if<And<Default<T>...>::value, int>::type = 0>
    tup()
    {
        printf("tup %d\n", (int)sizeof...(T));
    }
};
template <class T> struct one {};
template <class... T> struct many {};
template <template <class...> class A = one> void g() { printf("g\n"); }
template <template <class> class A> void h() { printf("h\n"); }
int main()
{
    tup<int, char> a;
    tup<int, int> b;
    tup<long> c;
    g<>();
    g<many>();
    h<one>();
    h<many>();
}
