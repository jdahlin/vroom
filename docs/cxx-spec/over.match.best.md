# 12.4.3 Best viable function [over.match.best]

[Contents](README.md) · up: [12.4 Overload resolution \[over.match\]](over.match.md) · prev: [12.4.2 Viable functions \[over.match.viable\]](over.match.viable.md) · next: [12.4.3.1 Implicit conversion sequences \[over.best.ics\]](over.best.ics.md)

**1** Define ICS*i*(`F`) as follows:

- If `F` is a static member function, ICS*1*(`F`) is defined such that ICS*1*(`F`) is neither better nor worse than ICS*1*(`G`) for any function `G`, and, symmetrically, ICS*1*(`G`) is neither better nor worse than ICS*1*(`F`);[^1] otherwise,
- let ICS*i*(`F`) denote the implicit conversion sequence that converts the *i*-th argument in the list to the type of the *i*-th parameter of viable function `F`. [[over.best.ics]](over.best.ics.md) defines the implicit conversion sequences and [[over.ics.rank]](over.ics.rank.md) defines what it means for one implicit conversion sequence to be a better conversion sequence or worse conversion sequence than another.

**2** Given these definitions, a viable function `F1` is defined to be a *better* function than another viable function `F2` if for all arguments *i*, ICS*i*(`F1`) is not a worse conversion sequence than ICS*i*(`F2`), and then

- for some argument *j*, ICS*j*(`F1`) is a better conversion sequence than ICS*j*(`F2`), or, if not that,

- the context is an initialization by user-defined conversion (see [[dcl.init]](dcl.init.md), [[over.match.conv]](over.match.conv.md), and [[over.match.ref]](over.match.ref.md)) and the standard conversion sequence from the return type of `F1` to the destination type (i.e., the type of the entity being initialized) is a better conversion sequence than the standard conversion sequence from the return type of `F2` to the destination type [*Example 1*:

  ```cpp
  struct A {
    A();
    operator int();
    operator double();
  } a;
  int i = a;          // a.operator int() followed by no conversion is better than
                      // a.operator double() followed by a conversion to int
  float x = a;        // ambiguous: both possibilities require conversions,
                      // and neither is better than the other
  ```

  — *end example*] or, if not that,

- the context is an initialization by conversion function for direct reference binding ([[over.match.ref]](over.match.ref.md)) of a reference to function type, the return type of `F1` is the same kind of reference (lvalue or rvalue) as the reference being initialized, and the return type of `F2` is not [*Example 2*:

  ```cpp
  template <class T> struct A {
    operator T&();    // #1
    operator T&&();   // #2
  };
  typedef int Fn();
  A<Fn> a;
  Fn& lf = a;         // calls #1
  Fn&& rf = a;        // calls #2
  ```

  — *end example*] or, if not that,

- `F1` is not a function template specialization and `F2` is a function template specialization, or, if not that,

- `F1` and `F2` are function template specializations, and the function template for `F1` is more specialized than the template for `F2` according to the partial ordering rules described in [[temp.func.order]](temp.func.order.md), or, if not that,

- `F1` and `F2` are non-template functions with the same parameter-type-lists, and `F1` is more constrained than `F2` according to the partial ordering of constraints described in [[temp.constr.order]](temp.constr.order.md), or if not that,

- `F1` is a constructor for a class `D`, `F2` is a constructor for a base class `B` of `D`, and for all arguments the corresponding parameters of `F1` and `F2` have the same type. [*Example 3*:

  ```cpp
  struct A {
    A(int = 0);
  };

  struct B: A {
    using A::A;
    B();
  };

  int main() {
    B b;              // OK, B::B()
  }
  ```

  — *end example*] or, if not that,

- `F2` is a rewritten candidate ([[over.match.oper]](over.match.oper.md)) and `F1` is not [*Example 4*:

  ```cpp
  struct S {
    friend auto operator<=>(const S&, const S&) = default;        // #1
    friend bool operator<(const S&, const S&);                    // #2
  };
  bool b = S() < S();                                             // calls #2
  ```

  — *end example*] or, if not that,

- `F1` and `F2` are rewritten candidates, and `F2` is a synthesized candidate with reversed order of parameters and `F1` is not [*Example 5*:

  ```cpp
  struct S {
    friend std::weak_ordering operator<=>(const S&, int);         // #1
    friend std::weak_ordering operator<=>(int, const S&);         // #2
  };
  bool b = 1 < S();                                               // calls #2
  ```

  — *end example*] or, if not that

- `F1` is generated from a *deduction-guide* ([[over.match.class.deduct]](over.match.class.deduct.md)) and `F2` is not, or, if not that,

- `F1` is the copy deduction candidate ([[over.match.class.deduct]](over.match.class.deduct.md)) and `F2` is not, or, if not that,

- `F1` is generated from a non-template constructor and `F2` is generated from a constructor template. [*Example 6*:

  ```cpp
  template <class T> struct A {
    using value_type = T;
    A(value_type);    // #1
    A(const A&);      // #2
    A(T, T, int);     // #3
    template<class U>
      A(int, T, U);   // #4
    // #5 is the copy deduction candidate, A(A)
  };

  A x(1, 2, 3);       // uses #3, generated from a non-template constructor

  template <class T>
  A(T) -> A<T>;       // #6, less specialized than #5

  A a(42);            // uses #6 to deduce A<int> and #1 to initialize
  A b = a;            // uses #5 to deduce A<int> and #2 to initialize

  template <class T>
  A(A<T>) -> A<A<T>>; // #7, as specialized as #5

  A b2 = a;           // uses #7 to deduce A<A<int>> and #1 to initialize
  ```

  — *end example*]

**3** If there is exactly one viable function that is a better function than all other viable functions, then it is the one selected by overload resolution; otherwise the call is ill-formed.[^2] [*Example 7*:

```cpp
void Fcn(const int*,  short);
void Fcn(int*, int);

int i;
short s = 0;

void f() {
  Fcn(&i, s);       // is ambiguous because &i → int* is better than &i → const int*
                    // but s → short is also better than s → int

  Fcn(&i, 1L);      // calls Fcn(int*, int), because &i → int* is better than &i → const int*
                    // and 1L → short and 1L → int are indistinguishable

  Fcn(&i, 'c');     // calls Fcn(int*, int), because &i → int* is better than &i → const int*
                    // and c → int is better than c → short
}
```

— *end example*]

**4** If the best viable function resolves to a function for which multiple declarations were found, and if at least two of these declarations — or the declarations they refer to in the case of *using-declaration*s — specify a default argument that made the function viable, the program is ill-formed. [*Example 8*:

```cpp
namespace A {
  extern "C" void f(int = 5);
}
namespace B {
  extern "C" void f(int = 5);
}

using A::f;
using B::f;

void use() {
  f(3);             // OK, default argument was not used for viability
  f();              // error: found default argument twice
}
```

— *end example*]

## Subclauses

- [12.4.3.1 Implicit conversion sequences \[over.best.ics\]](over.best.ics.md)
  - [12.4.3.1.1 Standard conversion sequences \[over.ics.scs\]](over.ics.scs.md)
  - [12.4.3.1.2 User-defined conversion sequences \[over.ics.user\]](over.ics.user.md)
  - [12.4.3.1.3 Ellipsis conversion sequences \[over.ics.ellipsis\]](over.ics.ellipsis.md)
  - [12.4.3.1.4 Reference binding \[over.ics.ref\]](over.ics.ref.md)
  - [12.4.3.1.5 List-initialization sequence \[over.ics.list\]](over.ics.list.md)
- [12.4.3.2 Ranking implicit conversion sequences \[over.ics.rank\]](over.ics.rank.md)

[^1]: If a function is a static member function, this definition means that the first argument, the implied object argument, has no effect in the determination of whether the function is better or worse than any other function.
[^2]: The algorithm for selecting the best viable function is linear in the number of viable functions. Run a simple tournament to find a function `W` that is not worse than any opponent it faced. Although another function `F` that `W` did not face might be at least as good as `W`, `F` cannot be the best function because at some point in the tournament `F` encountered another function `G` such that `F` was not better than `G`. Hence, either `W` is the best function or there is no best function. So, make a second pass over the viable functions to verify that `W` is better than all other functions.
