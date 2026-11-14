template<class T, T v> struct ic { static constexpr T value = v; };
template<class T> struct is_int : ic<bool, __is_integral(T)> {};
template<bool B, class T = void> struct en {};
template<class T> struct en<true, T> { typedef T type; };
template<bool B, class T = void> using en_t = typename en<B, T>::type;
template<class T, en_t<is_int<T>::value, int> = 0> long f(T) { return 1; }
template<class T, en_t<!is_int<T>::value, int> = 0> char f(T) { return 2; }
template<class T, class = en_t<is_int<T>::value>> short g(T) { return 3; }
template<int N> struct A { char c[N]; };
void h(int i, double d) {
  A<sizeof(f(i))> a8;
  A<sizeof(f(d))> a1;
  A<sizeof(g(i))> a2;
}
