// [temp.dep.type]/9: a name introduced by using typename from a dependent
// base is a dependent type; in a parenthesized expression the parser reads
// (at::p::value) as an expression, not as a cast (libc++'s __split_buffer).
template <class A> struct Base { using at = A; };
template <class A> struct Value { static constexpr bool value = true; };
template <class A>
struct Derived : Base<A> {
  using typename Base<A>::at;
  Derived& operator=(Derived&&) noexcept((at::p::value && Value<at>::value) || !at::p::value);
  static constexpr bool b = (at::p::value);
};

// C++23's explicit object parameter is a member function's: in a block,
// this after the ( of a declarator is the first argument of an initializer.
struct Local { Local(void *, int); };
struct Owner {
  void f();
};
void Owner::f() { Local tx(this, 1); }

// [temp.res]/4, [temp.dep.type]/6: a member of a dependent specialization of
// a template with no definition (libc++'s _IterOps) is no type without
// typename, so a statement starting with it is an expression, not a
// declaration of a name.
template <class P> struct incomplete_ops;
struct classic_policy {};
template <> struct incomplete_ops<classic_policy> {
  template <class I> static int move(I);
};
template <class P, class I> void sift(I last) {
  incomplete_ops<P>::move(last);
  int t(incomplete_ops<P>::move(last));
  int u = incomplete_ops<P>::move(last);
}

// [namespace.udecl]/1, [temp.dep.type]/6: a using-declaration of a member of
// a dependent base declares a name that is no type: a call of it in an
// out-of-class member definition is an expression (libc++'s __split_buffer).
template <class T> struct ud_base { static const int v = 1; int f(int); };
template <class T> struct ud_derived : ud_base<T> {
  using ud_base<T>::v;
  using ud_base<T>::f;
  int h(int);
};
template <class T> int ud_derived<T>::h(int p) {
  f(p);
  bool b = v < 3;
  return b + (v > 2);
}
