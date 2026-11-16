# 13.8 Name resolution [temp.res]

[Contents](README.md) · up: [13 Templates \[temp\]](temp.md) · prev: [13.7.8 Concept definitions \[temp.concept\]](temp.concept.md) · next: [13.8.1 Locally declared names \[temp.local\]](temp.local.md)

**1** Three kinds of names can be used within a template definition:

- The name of the template itself, and names declared within the template itself.
- Names dependent on a *template-parameter* ([[temp.dep]](temp.dep.md)).
- Names from scopes which are visible within the template definition.

**2** A name used in a template declaration or definition and that is dependent on a *template-parameter* is assumed not to name a type unless the applicable name lookup finds a type name or the name is qualified by the keyword `typename`. [*Example 1*:

```cpp
// no B declared here

class X;

template<class T> class Y {
  class Z;                      // forward declaration of member class

  void f() {
    X* a1;                      // declare pointer to X
    T* a2;                      // declare pointer to T
    Y* a3;                      // declare pointer to Y<T>
    Z* a4;                      // declare pointer to Z
    typedef typename T::A TA;
    TA* a5;                     // declare pointer to T's A
    typename T::A* a6;          // declare pointer to T's A
    T::A* a7;                   // error: no visible declaration of a7
                                // T::A is not a type name; multiplication of T::A by a7
    B* a8;                      // error: no visible declarations of B and a8
                                // B is not a type name; multiplication of B by a8
  }
};
```

— *end example*]

```bnf
typename-specifier:
  'typename' nested-name-specifier identifier
  'typename' nested-name-specifier 'templateₒₚₜ' simple-template-id
```

**3** A *typename-specifier* denotes the type or class template denoted by the *simple-type-specifier* ([[dcl.type.simple]](dcl.type.simple.md)) formed by omitting the keyword `typename`. The usual qualified name lookup ([[basic.lookup.qual]](basic.lookup.qual.md)) is used to find the *qualified-id* even in the presence of `typename`. [*Example 2*:

```cpp
struct A {
  struct X { };
  int X;
};
struct B {
  struct X { };
};
template<class T> void f(T t) {
  typename T::X x;
}
void foo() {
  A a;
  B b;
  f(b);             // OK: T::X refers to B::X
  f(a);             // error: T::X refers to the data member A::X not the struct A::X
}
```

— *end example*]

**4** A qualified name used as the name in a *class-or-decltype* ([[class.derived]](class.derived.md)) or an *elaborated-type-specifier* is implicitly assumed to name a type, without the use of the `typename` keyword. In a *nested-name-specifier* that immediately contains a *nested-name-specifier* that depends on a template parameter, the *identifier* or *simple-template-id* is implicitly assumed to name a type, without the use of the `typename` keyword. [*Note 1*: The `typename` keyword is not permitted by the syntax of these constructs. — *end note*]

**5** A *qualified-id* is assumed to name a type if

- it is a qualified name in a type-id-only context (see below), or

- it is a *decl-specifier* of the *decl-specifier-seq* of a

  - *simple-declaration* or a *function-definition* in namespace scope,
  - *member-declaration*,
  - *parameter-declaration* in a *member-declaration*[^1], unless that *parameter-declaration* appears in a default argument,
  - *parameter-declaration* in a *declarator* of a function or function template declaration whose *declarator-id* is qualified, unless that *parameter-declaration* appears in a default argument,
  - *parameter-declaration* in a *lambda-declarator* or *requirement-parameter-list*, unless that *parameter-declaration* appears in a default argument, or
  - *parameter-declaration* of a (non-type) *template-parameter*.

A qualified name is said to be in a *type-id-only context* if it appears in a *type-id*, *new-type-id*, or *defining-type-id* and the smallest enclosing *type-id*, *new-type-id*, or *defining-type-id* is a *new-type-id*, *defining-type-id*, *trailing-return-type*, default argument of a *type-parameter* of a template, or *type-id* of a `static_cast`, `const_cast`, `reinterpret_cast`, or `dynamic_cast`. [*Example 3*:

```cpp
template<class T> T::R f();             // OK, return type of a function declaration at global scope
template<class T> void f(T::R);         // ill-formed, no diagnostic required: attempt to declare
                                        // a void variable template
template<class T> struct S {
  using Ptr = PtrTraits<T>::Ptr;        // OK, in a defining-type-id
  T::R f(T::P p) {                      // OK, class scope
    return static_cast<T::R>(p);        // OK, type-id of a static_cast
  }
  auto g() -> S<T*>::Ptr;               // OK, trailing-return-type
};
template<typename T> void f() {
  void (*pf)(T::X);                     // variable pf of type void* initialized with T::X
  void g(T::X);                         // error: T::X at block scope does not denote a type
                                        // (attempt to declare a void variable)
}
```

— *end example*]

**6** A *qualified-id* that refers to a member of an unknown specialization, that is not prefixed by `typename`, and that is not otherwise assumed to name a type (see above) denotes a non-type. [*Example 4*:

```cpp
template <class T> void f(int i) {
  T::x * i;         // expression, not the declaration of a variable i
}

struct Foo {
  typedef int x;
};

struct Bar {
  static int const x = 5;
};

int main() {
  f<Bar>(1);        // OK
  f<Foo>(1);        // error: Foo::x is a type
}
```

— *end example*]

**7** Within the definition of a class template or within the definition of a member of a class template following the *declarator-id*, the keyword `typename` is not required when referring to a member of the current instantiation ([[temp.dep.type]](temp.dep.type.md)). [*Example 5*:

```cpp
template<class T> struct A {
  typedef int B;
  B b;              // OK, no typename required
};
```

— *end example*]

**8** The validity of a template may be checked prior to any instantiation. [*Note 2*: Knowing which names are type names allows the syntax of every template to be checked in this way. — *end note*] The program is ill-formed, no diagnostic required, if:

- no valid specialization can be generated for a template or a substatement of a constexpr if statement ([[stmt.if]](stmt.if.md)) within a template and the template is not instantiated, or

- no substitution of template arguments into a *type-constraint* or *requires-clause* would result in a valid expression, or

- every valid specialization of a variadic template requires an empty template parameter pack, or

- a hypothetical instantiation of a template immediately following its definition would be ill-formed due to a construct that does not depend on a template parameter, or

- the interpretation of such a construct in the hypothetical instantiation is different from the interpretation of the corresponding construct in any actual instantiation of the template. [*Note 3*: This can happen in situations including the following:

  - a type used in a non-dependent name is incomplete at the point at which a template is defined but is complete at the point at which an instantiation is performed, or

  - lookup for a name in the template definition found a *using-declaration*, but the lookup in the corresponding scope in the instantiation does not find any declarations because the *using-declaration* was a pack expansion and the corresponding pack is empty, or

  - an instantiation uses a default argument or default template argument that had not been defined at the point at which the template was defined, or

  - constant expression evaluation ([[expr.const]](expr.const.md)) within the template instantiation uses

    - the value of a const object of integral or unscoped enumeration type or
    - the value of a `constexpr` object or
    - the value of a reference or
    - the definition of a constexpr function,

    and that entity was not defined when the template was defined, or

  - a class template specialization or variable template specialization that is specified by a non-dependent *simple-template-id* is used by the template, and either it is instantiated from a partial specialization that was not defined when the template was defined or it names an explicit specialization that was not declared when the template was defined.

  — *end note*]

Otherwise, no diagnostic shall be issued for a template for which a valid specialization can be generated. [*Note 4*: If a template is instantiated, errors will be diagnosed according to the other rules in this document. Exactly when these errors are diagnosed is a quality of implementation issue. — *end note*] [*Example 6*:

```cpp
int j;
template<class T> class X {
  void f(T t, int i, char* p) {
    t = i;          // diagnosed if X::f is instantiated, and the assignment to t is an error
    p = i;          // may be diagnosed even if X::f is not instantiated
    p = j;          // may be diagnosed even if X::f is not instantiated
  }
  void g(T t) {
    +;              // may be diagnosed even if X::g is not instantiated
  }
};

template<class... T> struct A {
  void operator++(int, T... t);                     // error: too many parameters
};
template<class... T> union X : T... { };            // error: union with base class
template<class... T> struct A : T...,  T... { };    // error: duplicate base class
```

— *end example*]

**9** When looking for the declaration of a name used in a template definition, the usual lookup rules ([[basic.lookup.unqual]](basic.lookup.unqual.md), [[basic.lookup.argdep]](basic.lookup.argdep.md)) are used for non-dependent names. The lookup of names dependent on the template parameters is postponed until the actual template argument is known ([[temp.dep]](temp.dep.md)). [*Example 7*:

```cpp
#include <iostream>
using namespace std;

template<class T> class Set {
  T* p;
  int cnt;
public:
  Set();
  Set<T>(const Set<T>&);
  void printall() {
    for (int i = 0; i<cnt; i++)
      cout << p[i] << '\n';
  }
};
```

In the example, `i` is the local variable `i` declared in `printall`, `cnt` is the member `cnt` declared in `Set`, and `cout` is the standard output stream declared in `iostream`. However, not every declaration can be found this way; the resolution of some names must be postponed until the actual *template-argument*s are known. For example, even though the name `operator<<` is known within the definition of `printall()` and a declaration of it can be found in `<iostream>`, the actual declaration of `operator<<` needed to print `p[i]` cannot be known until it is known what type `T` is ([[temp.dep]](temp.dep.md)). — *end example*]

**10** If a name does not depend on a *template-parameter* (as defined in [[temp.dep]](temp.dep.md)), a declaration (or set of declarations) for that name shall be in scope at the point where the name appears in the template definition; the name is bound to the declaration (or declarations) found at that point and this binding is not affected by declarations that are visible at the point of instantiation. [*Example 8*:

```cpp
void f(char);

template<class T> void g(T t) {
  f(1);             // f(char)
  f(T(1));          // dependent
  f(t);             // dependent
  dd++;             // not dependent; error: declaration for dd not found
}

enum E { e };
void f(E);

double dd;
void h() {
  g(e);             // will cause one call of f(char) followed by two calls of f(E)
  g('a');           // will cause three calls of f(char)
}
```

— *end example*]

**11** [*Note 5*: For purposes of name lookup, default arguments and *noexcept-specifier*s of function templates and default arguments and *noexcept-specifier*s of member functions of class templates are considered definitions ([[temp.decls]](temp.decls.md)). — *end note*]

## Subclauses

- [13.8.1 Locally declared names \[temp.local\]](temp.local.md)
- [13.8.2 Dependent names \[temp.dep\]](temp.dep.md)
  - [13.8.2.1 Dependent types \[temp.dep.type\]](temp.dep.type.md)
  - [13.8.2.2 Type-dependent expressions \[temp.dep.expr\]](temp.dep.expr.md)
  - [13.8.2.3 Value-dependent expressions \[temp.dep.constexpr\]](temp.dep.constexpr.md)
  - [13.8.2.4 Dependent template arguments \[temp.dep.temp\]](temp.dep.temp.md)
- [13.8.3 Non-dependent names \[temp.nondep\]](temp.nondep.md)
- [13.8.4 Dependent name resolution \[temp.dep.res\]](temp.dep.res.md)
  - [13.8.4.1 Point of instantiation \[temp.point\]](temp.point.md)
  - [13.8.4.2 Candidate functions \[temp.dep.candidate\]](temp.dep.candidate.md)
- [13.8.5 Friend names declared within a class template \[temp.inject\]](temp.inject.md)

[^1]: This includes friend function declarations.
