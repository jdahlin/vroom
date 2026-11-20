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
