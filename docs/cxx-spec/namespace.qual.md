# 6.5.3.2 Namespace members [namespace.qual]

[Contents](README.md) · up: [6.5.3 Qualified name lookup \[basic.lookup.qual\]](basic.lookup.qual.md) · prev: [6.5.3.1 Class members \[class.qual\]](class.qual.md) · next: [6.5.4 Elaborated type specifiers \[basic.lookup.elab\]](basic.lookup.elab.md)

**1** If the *nested-name-specifier* of a *qualified-id* nominates a namespace (including the case where the *nested-name-specifier* is `::`, i.e., nominating the global namespace), the name specified after the *nested-name-specifier* is looked up in the scope of the namespace. The names in a *template-argument* of a *template-id* are looked up in the context in which the entire *postfix-expression* occurs.

**2** For a namespace `X` and name `m`, the namespace-qualified lookup set *S*(*X*, *m*) is defined as follows: Let *S*'(*X*, *m*) be the set of all declarations of `m` in `X` and the inline namespace set of `X` ([[namespace.def]](namespace.def.md)) whose potential scope ([[basic.scope.namespace]](basic.scope.namespace.md)) would include the namespace in which `m` is declared at the location of the *nested-name-specifier*. If *S*'(*X*, *m*) is not empty, *S*(*X*, *m*) is *S*'(*X*, *m*); otherwise, *S*(*X*, *m*) is the union of *S*(*N*<sub>*i*</sub>, *m*) for all namespaces *N*<sub>*i*</sub> nominated by *using-directive*s in `X` and its inline namespace set.

**3** Given `X::m` (where `X` is a user-declared namespace), or given `::m` (where X is the global namespace), if *S*(*X*, *m*) is the empty set, the program is ill-formed. Otherwise, if *S*(*X*, *m*) has exactly one member, or if the context of the reference is a *using-declaration* ([[namespace.udecl]](namespace.udecl.md)), *S*(*X*, *m*) is the required set of declarations of `m`. Otherwise if the use of `m` is not one that allows a unique declaration to be chosen from *S*(*X*, *m*), the program is ill-formed. [*Example 1*:

```cpp
int x;
namespace Y {
  void f(float);
  void h(int);
}

namespace Z {
  void h(double);
}

namespace A {
  using namespace Y;
  void f(int);
  void g(int);
  int i;
}

namespace B {
  using namespace Z;
  void f(char);
  int i;
}

namespace AB {
  using namespace A;
  using namespace B;
  void g();
}

void h()
{
  AB::g();          // g is declared directly in AB, therefore S is { AB::g() } and AB::g() is chosen

  AB::f(1);         // f is not declared directly in AB so the rules are applied recursively to A and B;
                    // namespace Y is not searched and Y::f(float) is not considered;
                    // S is { A::f(int), B::f(char) } and overload resolution chooses A::f(int)

  AB::f('c');       // as above but resolution chooses B::f(char)

  AB::x++;          // x is not declared directly in AB, and is not declared in A or B, so the rules
                    // are applied recursively to Y and Z, S is { } so the program is ill-formed

  AB::i++;          // i is not declared directly in AB so the rules are applied recursively to A and B,
                    // S is { A::i, B::i } so the use is ambiguous and the program is ill-formed

  AB::h(16.8);      // h is not declared directly in AB and not declared directly in A or B so the rules
                    // are applied recursively to Y and Z, S is { Y::h(int), Z::h(double) } and
                    // overload resolution chooses Z::h(double)
}
```

— *end example*]

**4** [*Note 1*: The same declaration found more than once is not an ambiguity (because it is still a unique declaration). [*Example 2*:

```cpp
namespace A {
  int a;
}

namespace B {
  using namespace A;
}

namespace C {
  using namespace A;
}

namespace BC {
  using namespace B;
  using namespace C;
}

void f()
{
  BC::a++;          // OK: S is { A::a, A::a }
}

namespace D {
  using A::a;
}

namespace BD {
  using namespace B;
  using namespace D;
}

void g()
{
  BD::a++;          // OK: S is { A::a, A::a }
}
```

— *end example*] — *end note*]

**5** [*Example 3*: Because each referenced namespace is searched at most once, the following is well-defined:

```cpp
namespace B {
  int b;
}

namespace A {
  using namespace B;
  int a;
}

namespace B {
  using namespace A;
}

void f()
{
  A::a++;           // OK: a declared directly in A, S is { A::a }
  B::a++;           // OK: both A and B searched (once), S is { A::a }
  A::b++;           // OK: both A and B searched (once), S is { B::b }
  B::b++;           // OK: b declared directly in B, S is { B::b }
}
```

— *end example*]

**6** During the lookup of a qualified namespace member name, if the lookup finds more than one declaration of the member, and if one declaration introduces a class name or enumeration name and the other declarations introduce either the same variable, the same enumerator, or a set of functions, the non-type name hides the class or enumeration name if and only if the declarations are from the same namespace; otherwise (the declarations are from different namespaces), the program is ill-formed. [*Example 4*:

```cpp
namespace A {
  struct x { };
  int x;
  int y;
}

namespace B {
  struct y { };
}

namespace C {
  using namespace A;
  using namespace B;
  int i = C::x;     // OK, A::x (of type int)
  int j = C::y;     // ambiguous, A::y or B::y
}
```

— *end example*]

**7** In a declaration for a namespace member in which the *declarator-id* is a *qualified-id*, given that the *qualified-id* for the namespace member has the form

```bnf
nested-name-specifier unqualified-id
```

the *unqualified-id* shall name a member of the namespace designated by the *nested-name-specifier* or of an element of the inline namespace set ([[namespace.def]](namespace.def.md)) of that namespace. [*Example 5*:

```cpp
namespace A {
  namespace B {
    void f1(int);
  }
  using namespace B;
}
void A::f1(int){ }  // error: f1 is not a member of A
```

— *end example*] However, in such namespace member declarations, the *nested-name-specifier* may rely on *using-directive*s to implicitly provide the initial part of the *nested-name-specifier*. [*Example 6*:

```cpp
namespace A {
  namespace B {
    void f1(int);
  }
}

namespace C {
  namespace D {
    void f1(int);
  }
}

using namespace A;
using namespace C::D;
void B::f1(int){ }  // OK, defines A::B::f1(int)
```

— *end example*]
