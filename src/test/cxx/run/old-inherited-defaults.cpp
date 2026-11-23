// a function template defined after its declaration: the default template arguments the declaration
// gave (the definition may not repeat them), here enable_if-style after a pack
template <bool B, class T = void> struct en {};
template <class T> struct en<true, T> { typedef T type; };
template <class T> struct is_int { static const bool value = false; };
template <> struct is_int<int> { static const bool value = true; };
int n = 0;
template <class O> void put(O &) {}
template <class O, class A, class... R, typename en<is_int<A>::value, int>::type = 0> void put(O &o, A a, R... r);
template <class O, class A, class... R, typename en<!is_int<A>::value, int>::type = 0> void put(O &o, A a, R... r);
template <class O, class A, class... R, typename en<is_int<A>::value, int>::type> void put(O &o, A a, R... r) { o += a; put(o, r...); }
template <class O, class A, class... R, typename en<!is_int<A>::value, int>::type> void put(O &o, A a, R... r) { o += 100; put(o, r...); }
int main() { int o = 0; put(o, 1, 2L, 3); return o == 104 ? 0 : 1; }
