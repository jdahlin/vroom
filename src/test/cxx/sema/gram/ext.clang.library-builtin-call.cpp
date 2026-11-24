// RUN: %clang_cc1 -std=c++17 -fsyntax-only %s
// [ext.clang] clang's C++ library builtins (Builtins.td: std::move,
// std::forward, std::addressof, ...): a call of one is the builtin's, whose
// definition is not instantiated; naming one otherwise (its address) is a
// use that instantiates it.  std::move(T, T) is not the builtin, nor is
// std::forward<T &> of an rvalue (the rvalue overload)
namespace std {
template <class T> struct remove_reference { using type = T; };
template <class T> struct remove_reference<T &> { using type = T; };
template <class T> T &&move(T &x) {
  static_assert(T::moveable, ""); // expected-error {{no member named 'moveable' in 'B'}}
  return static_cast<T &&>(x);
}
template <class T> int move(T, T) { return 5; }
template <class T> T &&forward(typename remove_reference<T>::type &x) {
  return static_cast<T &&>(x);
}
template <class T> T &&forward(typename remove_reference<T>::type &&x) {
  static_assert(T::moveable, ""); // expected-error {{cannot be used prior to '::'}}
  return static_cast<T &&>(x);
}
template <class T> T *addressof(T &x) {
  static_assert(T::moveable, "");
  return __builtin_addressof(x);
}
} // namespace std
struct A {};
struct B {};
void calls() {
  int n = 0;
  std::move(n); // expected-warning {{ignoring return value}}
  std::addressof(n); // expected-warning {{ignoring return value}}
  std::forward<A &&>(A()); // expected-warning {{ignoring return value}}
  int k = std::move(n, n);
}
void rvalue_as_lvalue() { std::forward<A &>(A()); } // expected-note {{in instantiation}} expected-warning {{ignoring return value}}
B &&(*named)(B &) = std::move; // expected-note {{in instantiation}} expected-warning {{non-addressable}}
// a non-constexpr builtin is no constant expression; std::addressof is one
constexpr bool f(int a) { // expected-error {{never produces a constant expression}}
  int &&m = std::move(a); // expected-note {{subexpression not valid}}
  return true;
}
constexpr bool g(int a) { return std::addressof(a) != nullptr; }
