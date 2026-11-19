// std: c++20
// a namespace's functions and its inline namespace's by the same name are one overload set (libc++:
// std::begin(initializer_list) in std, std::begin(C&) in std::__1); a, b with a class operand and no
// operator, is the built-in comma (decltype(begin(c), end(c)) in json's input_adapter detection)
namespace s {
template <class E> struct il { const E *p; };
template <class E> const E *bg(il<E> x) { return x.p; }
inline namespace v1 {
template <class C> auto bg(C &c) -> decltype(c.begin()) { return c.begin(); }
}
}
struct X { int a[2]; int *begin() { return a; } };
template <class T> T &&dv();
template <class C, class = void> struct F { static constexpr int v = 0; };
template <class C> struct F<C, decltype((void)s::bg(dv<C &>()), (void)0)> { static constexpr int v = 1; };
int main() {
  X x;
  x.a[0] = 1, x.a[1] = 2;
  int i = 3;
  s::il<int> l{&i};
  return *s::bg(x) == 1 && *s::bg(l) == 3 && F<X>::v == 1 && F<int>::v == 0 && sizeof(decltype(s::bg(x), 2L)) == 8 ? 0 : 1;
}
