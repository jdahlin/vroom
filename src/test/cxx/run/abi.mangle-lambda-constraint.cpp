/* [abi:mangling-compression] a type-constraint and requires-clauses of
   function and generic lambda templates: Tk, C<> as C, a lambda's own
   parameter in its clause TL0__ and not the candidate the signature's
   const U is */
/* cxx-run-flags: -std=c++20 */
struct S { int x; };
template <class T> concept C = true;
template <class A, class B> concept D = true;
template <class U> int a(const U &u) requires C<const U> { return u.x; }
template <class U> requires C<const U> int b(const U &u) { return u.x; }
template <class U> requires C<U &> && C<const U &> int c(const U &u, U &v) { return u.x; }
template <C<> U> int d(const U &u) requires D<const U, U> { return u.x; }
template <class U> int e(const U &u) requires requires { requires C<const U>; } { return u.x; }
template <class U> int f(const U &u) requires requires { {u} -> D<const U>; } { return u.x; }
template <class U> int g(U *u) requires C<U *> && D<const U, U const *> { return u->x; }
template <class T> int call(const T &t) {
  auto l = []<class U>(const U &u) requires C<const U> { return u.x; };
  auto m = []<class U> requires C<const U> (const U &u) { return u.x; };
  auto n = []<C U>(const U &u) { return u.x; };
  auto o = [](const C auto &u) { return u.x; };
  auto p = []<class U>(U u) requires C<const U &> { return u.x; };
  return l(t) + m(t) + n(t) + o(t) + p(t);
}
int main() { S s{3}; return a(s) + b(s) + c(s, s) + d(s) + g(&s) + call(s) - 30; }
