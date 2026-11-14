// a pack followed by defaulted parameters (template <class T, class... A, enable_if<...>::type = 0>): deduced,
// the defaults computed with the pack bound, a parameter whose type does not form makes the candidate not viable
template <bool B, class T = void> struct en {};
template <class T> struct en<true, T> { using type = T; };
template <class T> struct is_int { static constexpr bool value = false; };
template <> struct is_int<int> { static constexpr bool value = true; };
template <class T, class... A, typename en<is_int<T>::value, int>::type = 0> int put(T *p, A... a) { *p = (int)sizeof...(A); return 1; }
template <class T, class... A, typename en<!is_int<T>::value, int>::type = 0> int put(T *p, A... a) { return 2; }
int main() { int x = 0; long y = 0; return put(&x, 2, 3) == 1 && put(&y, 1) == 2 ? 0 : 1; }
