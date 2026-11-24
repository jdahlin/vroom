// RUN: %clang_cc1 -std=c++17 -fsyntax-only %s
// [temp.inst]/4, [temp.res]/8: the use that first instantiates a
// specialization's definition is ill-formed when the instantiated body has
// an error naming a template argument (clang's "in instantiation of ...
// requested here"), a constexpr template's (instantiated at the use) as a
// non-constexpr one's (at the end of the unit); later uses of the same
// specialization are not (a constexpr one's are: clang's substitution
// failure, not analysed)
template <typename T> constexpr T &&mv(T &x) {
  static_assert(T::moveable, ""); // expected-error {{no member named 'moveable' in 'B'}}
  return static_cast<T &&>(x);
}
template <typename T> T &&mv2(T &x) {
  static_assert(T::moveable, ""); // expected-error {{no member named 'moveable' in 'B'}}
  return static_cast<T &&>(x);
}
struct B {};
B &&(*p1)(B &) = mv; // expected-note {{in instantiation}}
B &&(*p2)(B &) = mv2; // expected-note {{in instantiation}}
void g() { B b; mv2(b); }
