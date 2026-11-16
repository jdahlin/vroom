# 13.3 Names of template specializations [temp.names]

[Contents](../README.md) · up: [13 Templates \[temp\]](temp.md) · prev: [13.2 Template parameters \[temp.param\]](temp.param.md) · next: [13.4 Template arguments \[temp.arg\]](temp.arg.md)

**1** A template specialization ([[temp.spec]](temp.spec.md)) can be referred to by a *template-id*:

```bnf
simple-template-id:
  template-name '<' template-argument-listₒₚₜ '>'
```

```bnf
template-id:
  simple-template-id
  operator-function-id '<' template-argument-listₒₚₜ '>'
  literal-operator-id '<' template-argument-listₒₚₜ '>'
```

```bnf
template-name:
  identifier
```

```bnf
template-argument-list:
  template-argument '...'ₒₚₜ
  template-argument-list ',' template-argument '...'ₒₚₜ
```

```bnf
template-argument:
  constant-expression
  type-id
  id-expression
```

[*Note 1*: The name lookup rules ([[basic.lookup]](../basic/basic.lookup.md)) are used to associate the use of a name with a template declaration; that is, to identify a name as a *template-name*. — *end note*]

**2** For a *template-name* to be explicitly qualified by the template arguments, the name must be considered to refer to a template. [*Note 2*: Whether a name actually refers to a template cannot be known in some cases until after argument dependent lookup is done ([[basic.lookup.argdep]](../basic/basic.lookup.argdep.md)). — *end note*] A name is considered to refer to a template if name lookup finds a *template-name* or an overload set that contains a function template. A name is also considered to refer to a template if it is an *unqualified-id* followed by a `<` and name lookup either finds one or more functions or finds nothing.

**3** When a name is considered to be a *template-name*, and it is followed by a `<`, the `<` is always taken as the delimiter of a *template-argument-list* and never as the less-than operator. When parsing a *template-argument-list*, the first non-nested `>`[^1] is taken as the ending delimiter rather than a greater-than operator. Similarly, the first non-nested `>>` is treated as two consecutive but distinct `>` tokens, the first of which is taken as the end of the *template-argument-list* and completes the *template-id*. [*Note 3*: The second `>` token produced by this replacement rule may terminate an enclosing *template-id* construct or it may be part of a different construct (e.g., a cast). — *end note*] [*Example 1*:

```cpp
template<int i> class X { /* ... */ };

X< 1>2 > x1;                            // syntax error
X<(1>2)> x2;                            // OK

template<class T> class Y { /* ... */ };
Y<X<1>> x3;                             // OK, same as Y<X<1> > x3;
Y<X<6>>1>> x4;                          // syntax error
Y<X<(6>>1)>> x5;                        // OK
```

— *end example*]

**4** The keyword `template` is said to appear at the top level in a *qualified-id* if it appears outside of a *template-argument-list* or *decltype-specifier*. In a *qualified-id* of a *declarator-id* or in a *qualified-id* formed by a *class-head-name* ([[class.pre]](../class/class.pre.md)) or *enum-head-name* ([[dcl.enum]](../dcl/dcl.enum.md)), the keyword `template` shall not appear at the top level. In a *qualified-id* used as the name in a *typename-specifier* ([[temp.res]](temp.res.md)), *elaborated-type-specifier* ([[dcl.type.elab]](../dcl/dcl.type.elab.md)), *using-declaration* ([[namespace.udecl]](../dcl/namespace.udecl.md)), or *class-or-decltype* ([[class.derived]](../class/class.derived.md)), an optional keyword `template` appearing at the top level is ignored. In these contexts, a `<` token is always assumed to introduce a *template-argument-list*. In all other contexts, when naming a template specialization of a member of an unknown specialization ([[temp.dep.type]](temp.dep.type.md)), the member template name shall be prefixed by the keyword `template`. [*Example 2*:

```cpp
struct X {
  template<std::size_t> X* alloc();
  template<std::size_t> static X* adjust();
};
template<class T> void f(T* p) {
  T* p1 = p->alloc<200>();              // error: < means less than
  T* p2 = p->template alloc<200>();     // OK: < starts template argument list
  T::adjust<100>();                     // error: < means less than
  T::template adjust<100>();            // OK: < starts template argument list
}
```

— *end example*]

**5** A name prefixed by the keyword `template` shall be a *template-id* or the name shall refer to a class template or an alias template. [*Note 4*: The keyword `template` may not be applied to non-template members of class templates. — *end note*] [*Note 5*: As is the case with the `typename` prefix, the `template` prefix is allowed in cases where it is not strictly necessary; i.e., when the *nested-name-specifier* or the expression on the left of the `->` or `.` is not dependent on a *template-parameter*, or the use does not appear in the scope of a template. — *end note*] [*Example 3*:

```cpp
template <class T> struct A {
  void f(int);
  template <class U> void f(U);
};

template <class T> void f(T t) {
  A<T> a;
  a.template f<>(t);                    // OK: calls template
  a.template f(t);                      // error: not a template-id
}

template <class T> struct B {
  template <class T2> struct C { };
};

// OK: T::template C names a class template:
template <class T, template <class X> class TT = T::template C> struct D { };
D<B<int> > db;
```

— *end example*]

**6** A *template-id* is *valid* if

- there are at most as many arguments as there are parameters or a parameter is a template parameter pack ([[temp.variadic]](temp.variadic.md)),
- there is an argument for each non-deducible non-pack parameter that does not have a default *template-argument*,
- each *template-argument* matches the corresponding *template-parameter* ([[temp.arg]](temp.arg.md)),
- substitution of each template argument into the following template parameters (if any) succeeds, and
- if the *template-id* is non-dependent, the associated constraints are satisfied as specified in the next paragraph.

A *simple-template-id* shall be valid unless it names a function template specialization ([[temp.deduct]](temp.deduct.md)). [*Example 4*:

```cpp
template<class T, T::type n = 0> class X;
struct S {
  using type = int;
};
using T1 = X<S, int, int>;      // error: too many arguments
using T2 = X<>;                 // error: no default argument for first template parameter
using T3 = X<1>;                // error: value 1 does not match type-parameter
using T4 = X<int>;              // error: substitution failure for second template parameter
using T5 = X<S>;                // OK
```

— *end example*]

**7** When the *template-name* of a *simple-template-id* names a constrained non-function template or a constrained template *template-parameter*, but not a member template that is a member of an unknown specialization ([[temp.res]](temp.res.md)), and all *template-argument*s in the *simple-template-id* are non-dependent ([[temp.dep.temp]](temp.dep.temp.md)), the associated constraints ([[temp.constr.decl]](temp.constr.decl.md)) of the constrained template shall be satisfied ([[temp.constr.constr]](temp.constr.constr.md)). [*Example 5*:

```cpp
template<typename T> concept C1 = sizeof(T) != sizeof(int);

template<C1 T> struct S1 { };
template<C1 T> using Ptr = T*;

S1<int>* p;                         // error: constraints not satisfied
Ptr<int> p;                         // error: constraints not satisfied

template<typename T>
struct S2 { Ptr<int> x; };          // ill-formed, no diagnostic required

template<typename T>
struct S3 { Ptr<T> x; };            // OK, satisfaction is not required

S3<int> x;                          // error: constraints not satisfied

template<template<C1 T> class X>
struct S4 {
  X<int> x;                         // ill-formed, no diagnostic required
};

template<typename T> concept C2 = sizeof(T) == 1;

template<C2 T> struct S { };

template struct S<char[2]>;         // error: constraints not satisfied
template<> struct S<char[2]> { };   // error: constraints not satisfied
```

— *end example*]

**8** A *concept-id* is a *simple-template-id* where the *template-name* is a *concept-name*. A concept-id is a prvalue of type `bool`, and does not name a template specialization. A concept-id evaluates to `true` if the concept’s normalized *constraint-expression* ([[temp.constr.decl]](temp.constr.decl.md)) is satisfied ([[temp.constr.constr]](temp.constr.constr.md)) by the specified template arguments and `false` otherwise. [*Note 6*: Since a *constraint-expression* is an unevaluated operand, a concept-id appearing in a *constraint-expression* is not evaluated except as necessary to determine whether the normalized constraints are satisfied. — *end note*] [*Example 6*:

```cpp
template<typename T> concept C = true;
static_assert(C<int>);      // OK
```

— *end example*]

[^1]: A `>` that encloses the *type-id* of a `dynamic_cast`, `static_cast`, `reinterpret_cast` or `const_cast`, or which encloses the *template-argument*s of a subsequent *template-id*, is considered nested for the purpose of this description.
