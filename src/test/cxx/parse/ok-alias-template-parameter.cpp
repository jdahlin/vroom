template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <bool _Bp, class _Tp = void> using __enable_if_t [[__gnu__::__nodebug__]] = typename enable_if<_Bp, _Tp>::type;
template <class T> struct is_integral { static const bool value = true; };
template <class _Integer, __enable_if_t<is_integral<_Integer>::value, int> = 0> int f(_Integer);
