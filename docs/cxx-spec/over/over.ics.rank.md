# 12.4.3.2 Ranking implicit conversion sequences [over.ics.rank]

[Contents](../README.md) · up: [12.4.3 Best viable function \[over.match.best\]](over.match.best.md) · prev: [12.4.3.1.5 List-initialization sequence \[over.ics.list\]](over.ics.list.md) · next: [12.5 Address of overloaded function \[over.over\]](over.over.md)

**1** This subclause defines a partial ordering of implicit conversion sequences based on the relationships *better conversion sequence* and *better conversion*. If an implicit conversion sequence S1 is defined by these rules to be a better conversion sequence than S2, then it is also the case that S2 is a *worse conversion sequence* than S1. If conversion sequence S1 is neither better than nor worse than conversion sequence S2, S1 and S2 are said to be *indistinguishable conversion sequences*.

**2** When comparing the basic forms of implicit conversion sequences (as defined in [[over.best.ics]](over.best.ics.md))

- a standard conversion sequence ([[over.ics.scs]](over.ics.scs.md)) is a better conversion sequence than a user-defined conversion sequence or an ellipsis conversion sequence, and
- a user-defined conversion sequence ([[over.ics.user]](over.ics.user.md)) is a better conversion sequence than an ellipsis conversion sequence ([[over.ics.ellipsis]](over.ics.ellipsis.md)).

**3** Two implicit conversion sequences of the same form are indistinguishable conversion sequences unless one of the following rules applies:

- List-initialization sequence `L1` is a better conversion sequence than list-initialization sequence `L2` if

  - `L1` converts to `std::initializer_list<X>` for some `X` and `L2` does not, or, if not that,
  - `L1` and `L2` convert to arrays of the same element type, and either the number of elements *n*<sub>1</sub> initialized by `L1` is less than the number of elements *n*<sub>2</sub> initialized by `L2`, or *n*<sub>1</sub> = *n*<sub>2</sub> and `L2` converts to an array of unknown bound and `L1` does not,

  even if one of the other rules in this paragraph would otherwise apply. [*Example 1*:

  ```cpp
  void f1(int);                                   // #1
  void f1(std::initializer_list<long>);           // #2
  void g1() { f1({42}); }                         // chooses #2

  void f2(std::pair<const char*, const char*>);   // #3
  void f2(std::initializer_list<std::string>);    // #4
  void g2() { f2({"foo","bar"}); }                // chooses #4
  ```

  — *end example*] [*Example 2*:

  ```cpp
  void f(int    (&&)[] );         // #1
  void f(double (&&)[] );         // #2
  void f(int    (&&)[2]);         // #3

  f( {1} );           // Calls #1: Better than #2 due to conversion, better than #3 due to bounds
  f( {1.0} );         // Calls #2: Identity conversion is better than floating-integral conversion
  f( {1.0, 2.0} );    // Calls #2: Identity conversion is better than floating-integral conversion
  f( {1, 2} );        // Calls #3: Converting to array of known bound is better than to unknown bound,
                      // and an identity conversion is better than floating-integral conversion
  ```

  — *end example*]

- Standard conversion sequence `S1` is a better conversion sequence than standard conversion sequence `S2` if

  - `S1` is a proper subsequence of `S2` (comparing the conversion sequences in the canonical form defined by [[over.ics.scs]](over.ics.scs.md), excluding any Lvalue Transformation; the identity conversion sequence is considered to be a subsequence of any non-identity conversion sequence) or, if not that,

  - the rank of `S1` is better than the rank of `S2`, or `S1` and `S2` have the same rank and are distinguishable by the rules in the paragraph below, or, if not that,

  - `S1` and `S2` include reference bindings ([[dcl.init.ref]](../dcl/dcl.init.ref.md)) and neither refers to an implicit object parameter of a non-static member function declared without a *ref-qualifier*, and `S1` binds an rvalue reference to an rvalue and `S2` binds an lvalue reference [*Example 3*:

    ```cpp
    int i;
    int f1();
    int&& f2();
    int g(const int&);
    int g(const int&&);
    int j = g(i);                   // calls g(const int&)
    int k = g(f1());                // calls g(const int&&)
    int l = g(f2());                // calls g(const int&&)

    struct A {
      A& operator<<(int);
      void p() &;
      void p() &&;
    };
    A& operator<<(A&&, char);
    A() << 1;                       // calls A::operator<<(int)
    A() << 'c';                     // calls operator<<(A&&, char)
    A a;
    a << 1;                         // calls A::operator<<(int)
    a << 'c';                       // calls A::operator<<(int)
    A().p();                        // calls A::p()&&
    a.p();                          // calls A::p()&
    ```

    — *end example*] or, if not that,

  - `S1` and `S2` include reference bindings ([[dcl.init.ref]](../dcl/dcl.init.ref.md)) and `S1` binds an lvalue reference to a function lvalue and `S2` binds an rvalue reference to a function lvalue [*Example 4*:

    ```cpp
    int f(void(&)());               // #1
    int f(void(&&)());              // #2
    void g();
    int i1 = f(g);                  // calls #1
    ```

    — *end example*] or, if not that,

  - `S1` and `S2` differ only in their qualification conversion ([[conv.qual]](../expr/conv.qual.md)) and yield similar types `T1` and `T2`, respectively, where `T1` can be converted to `T2` by a qualification conversion. [*Example 5*:

    ```cpp
    int f(const volatile int *);
    int f(const int *);
    int i;
    int j = f(&i);                  // calls f(const int*)
    ```

    — *end example*] or, if not that,

  - `S1` and `S2` include reference bindings ([[dcl.init.ref]](../dcl/dcl.init.ref.md)), and the types to which the references refer are the same type except for top-level cv-qualifiers, and the type to which the reference initialized by `S2` refers is more cv-qualified than the type to which the reference initialized by `S1` refers. [*Example 6*:

    ```cpp
    int f(const int &);
    int f(int &);
    int g(const int &);
    int g(int);

    int i;
    int j = f(i);                   // calls f(int &)
    int k = g(i);                   // ambiguous

    struct X {
      void f() const;
      void f();
    };
    void g(const X& a, X b) {
      a.f();                        // calls X::f() const
      b.f();                        // calls X::f()
    }
    ```

    — *end example*]

- User-defined conversion sequence `U1` is a better conversion sequence than another user-defined conversion sequence `U2` if they contain the same user-defined conversion function or constructor or they initialize the same class in an aggregate initialization and in either case the second standard conversion sequence of `U1` is better than the second standard conversion sequence of `U2`. [*Example 7*:

  ```cpp
  struct A {
    operator short();
  } a;
  int f(int);
  int f(float);
  int i = f(a);                   // calls f(int), because short → int is
                                  // better than short → float.
  ```

  — *end example*]

**4** Standard conversion sequences are ordered by their ranks: an Exact Match is a better conversion than a Promotion, which is a better conversion than a Conversion. Two conversion sequences with the same rank are indistinguishable unless one of the following rules applies:

- A conversion that does not convert a pointer or a pointer to member to `bool` is better than one that does.

- A conversion that promotes an enumeration whose underlying type is fixed to its underlying type is better than one that promotes to the promoted underlying type, if the two are different.

- If class `B` is derived directly or indirectly from class `A`, conversion of `B*` to `A*` is better than conversion of `B*` to `void*`, and conversion of `A*` to `void*` is better than conversion of `B*` to `void*`.

- If class `B` is derived directly or indirectly from class `A` and class `C` is derived directly or indirectly from `B`,

  - conversion of `C*` to `B*` is better than conversion of `C*` to `A*`, [*Example 8*:

    ```cpp
    struct A {};
    struct B : public A {};
    struct C : public B {};
    C* pc;
    int f(A*);
    int f(B*);
    int i = f(pc);                  // calls f(B*)
    ```

    — *end example*]

  - binding of an expression of type `C` to a reference to type `B` is better than binding an expression of type `C` to a reference to type `A`,

  - conversion of `A::*` to `B::*` is better than conversion of `A::*` to `C::*`,

  - conversion of `C` to `B` is better than conversion of `C` to `A`,

  - conversion of `B*` to `A*` is better than conversion of `C*` to `A*`,

  - binding of an expression of type `B` to a reference to type `A` is better than binding an expression of type `C` to a reference to type `A`,

  - conversion of `B::*` to `C::*` is better than conversion of `A::*` to `C::*`, and

  - conversion of `B` to `A` is better than conversion of `C` to `A`.

  [*Note 1*: Compared conversion sequences will have different source types only in the context of comparing the second standard conversion sequence of an initialization by user-defined conversion (see [[over.match.best]](over.match.best.md)); in all other contexts, the source types will be the same and the target types will be different. — *end note*]
