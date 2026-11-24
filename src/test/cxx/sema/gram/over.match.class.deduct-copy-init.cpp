// [dcl.type.class.deduct]/1, [over.match.class.deduct]/3: a placeholder for a
// deduced class type with a copy-initializer (C x = e;) resolves the guides
// for the argument e; [over.match.copy]/1: an explicit guide is no candidate
// of copy-initialization (ill-formed when chosen).
template <typename T> struct A { A(T); };
template <typename T> A(T) -> A<T>;
A a1 = 0;
A a2 = {0};
A a3{0};
A a4(0);
static_assert(__is_same(decltype(a1), A<int>), "");
static_assert(__is_same(decltype(a2), A<int>), "");

template <typename T1 = int, typename T2 = int> struct B { B(T1 = 0, T2 = 0); };
template <typename T1, typename T2> explicit B(T1, T2) -> B<T1, T2>;  // expected-note {{explicit deduction guide declared here}}
B b1{0, 0};
B b2(0, 0);
B b3 = {0, 0}; // expected-error {{selected an explicit deduction guide for copy-list-initialization}}
