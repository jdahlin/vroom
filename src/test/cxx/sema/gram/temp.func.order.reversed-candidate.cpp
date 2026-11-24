// RUN: %clang_cc1 -std=c++20 -fsyntax-only %s
// [temp.func.order]/3 (CWG2445): against a synthesized candidate with
// reversed parameters, partial ordering reverses that template's function
// parameters: operator== of A<U *> reversed is more specialized than
// operator!=, which is not instantiated
template <typename> constexpr bool F = false;
template <typename T> struct A {};
template <typename T, typename U> bool operator==(T, A<U *>);
template <typename T, typename U> bool operator!=(A<T>, U) {
  static_assert(F<T>, "Isn't this less specialized?");
  return false;
}
bool f(A<int> ax, A<int *> ay) { return ay != ax; }
// expected-no-diagnostics
