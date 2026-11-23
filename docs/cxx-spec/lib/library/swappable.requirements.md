# 16.5.3.2 Swappable requirements [swappable.requirements]

[Contents](../../README.md) · up: [16.5.3 Requirements on types and expressions \[utility.requirements\]](utility.requirements.md) · prev: [16.5.3.1 Template argument requirements \[utility.arg.requirements\]](utility.arg.requirements.md) · next: [16.5.3.3 *Cpp17NullablePointer* requirements \[nullablepointer.requirements\]](nullablepointer.requirements.md)

**1** This subclause provides definitions for swappable types and expressions. In these definitions, let `t` denote an expression of type `T`, and let `u` denote an expression of type `U`.

**2** An object `t` is *swappable with* an object `u` if and only if:

- the expressions `swap(t, u)` and `swap(u, t)` are valid when evaluated in the context described below, and

- these expressions have the following effects:

  - the object referred to by `t` has the value originally held by `u` and
  - the object referred to by `u` has the value originally held by `t`.

**3** The context in which `swap(t, u)` and `swap(u, t)` are evaluated shall ensure that a binary non-member function named “swap” is selected via overload resolution ([[over.match]](../../over/over.match.md)) on a candidate set that includes:

- the two `swap` function templates defined in `<utility>` ([[utility.syn]](../utilities/utility.syn.md)) and
- the lookup set produced by argument-dependent lookup ([[basic.lookup.argdep]](../../basic/basic.lookup.argdep.md)).

[*Note 1*: If `T` and `U` are both fundamental types or arrays of fundamental types and the declarations from the header `<utility>` are in scope, the overall lookup set described above is equivalent to that of the qualified name lookup applied to the expression `std::swap(t, u)` or `std::swap(u, t)` as appropriate. — *end note*] [*Note 2*: It is unspecified whether a library component that has a swappable requirement includes the header `<utility>` to ensure an appropriate evaluation context. — *end note*]

**4** An rvalue or lvalue `t` is *swappable* if and only if `t` is swappable with any rvalue or lvalue, respectively, of type `T`.

**5** A type `X` meeting any of the iterator requirements ([[iterator.requirements]](../iterators/iterator.requirements.md)) meets the *Cpp17ValueSwappable* requirements if, for any dereferenceable object `x` of type `X`, `*x` is swappable.

**6** [*Example 1*: User code can ensure that the evaluation of `swap` calls is performed in an appropriate context under the various conditions as follows:

```cpp
#include <utility>

// Requires: std::forward<T>(t) shall be swappable with std::forward<U>(u).
template<class T, class U>
void value_swap(T&& t, U&& u) {
  using std::swap;
  swap(std::forward<T>(t), std::forward<U>(u)); // OK: uses ``swappable with'' conditions
                                                // for rvalues and lvalues
}

// Requires: lvalues of T shall be swappable.
template<class T>
void lv_swap(T& t1, T& t2) {
  using std::swap;
  swap(t1, t2);                                 // OK: uses swappable conditions for lvalues of type T
}

namespace N {
  struct A { int m; };
  struct Proxy { A* a; };
  Proxy proxy(A& a) { return Proxy{ &a }; }

  void swap(A& x, Proxy p) {
    std::swap(x.m, p.a->m);                     // OK: uses context equivalent to swappable
                                                // conditions for fundamental types
  }
  void swap(Proxy p, A& x) { swap(x, p); }      // satisfy symmetry constraint
}

int main() {
  int i = 1, j = 2;
  lv_swap(i, j);
  assert(i == 2 && j == 1);

  N::A a1 = { 5 }, a2 = { -5 };
  value_swap(a1, proxy(a2));
  assert(a1.m == -5 && a2.m == 5);
}
```

— *end example*]
