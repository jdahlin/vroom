/* A class whose definition cannot be analysed (here __is_nothrow_convertible, [ext.clang])
   and first named in an alias template's body, where the analysis drops what it
   cannot follow: a later use reports that failure, not a refusal of [temp.inst]
   of its own. */
template <class T, T v>
struct integral {
    static const T value = v;
};
template <class T, class U>
struct convertible : integral<bool, __is_nothrow_convertible(T, U)> {};

template <bool B, class T = void>
struct enable {};
template <class T>
struct enable<true, T> {
    typedef T type;
};
template <class T, class... Ts>
using pick = typename enable<convertible<int, long>::value, T>::type;

pick<int, char> x;
int y = convertible<int, long>::value;
