# 13.4.3 Template template arguments [temp.arg.template]

[Contents](README.md) · up: [13.4 Template arguments \[temp.arg\]](temp.arg.md) · prev: [13.4.2 Template non-type arguments \[temp.arg.nontype\]](temp.arg.nontype.md) · next: [13.5 Template constraints \[temp.constr\]](temp.constr.md)

**1** A *template-argument* for a template *template-parameter* shall be the name of a class template or an alias template, expressed as *id-expression*. When the *template-argument* names a class template, only primary class templates are considered when matching the template template argument with the corresponding parameter; partial specializations are not considered even if their parameter lists match that of the template template parameter.

**2** Any partial specializations ([[temp.class.spec]](temp.class.spec.md)) associated with the primary class template or primary variable template are considered when a specialization based on the template *template-parameter* is instantiated. If a specialization is not visible at the point of instantiation, and it would have been selected had it been visible, the program is ill-formed, no diagnostic required. [*Example 1*:

```cpp
template<class T> class A {     // primary template
  int x;
};
template<class T> class A<T*> { // partial specialization
  long x;
};
template<template<class U> class V> class C {
  V<int>  y;
  V<int*> z;
};
C<A> c;             // V<int> within C<A> uses the primary template, so c.y.x has type int
                    // V<int*> within C<A> uses the partial specialization, so c.z.x has type long
```

— *end example*]

**3** A *template-argument* matches a template *template-parameter* `P` when `P` is at least as specialized as the *template-argument* `A`. In this comparison, if `P` is unconstrained, the constraints on `A` are not considered. If `P` contains a template parameter pack, then `A` also matches `P` if each of `A`’s template parameters matches the corresponding template parameter in the *template-head* of `P`. Two template parameters match if they are of the same kind (type, non-type, template), for non-type *template-parameter*s, their types are equivalent ([[temp.over.link]](temp.over.link.md)), and for template *template-parameter*s, each of their corresponding *template-parameter*s matches, recursively. When `P`’s *template-head* contains a template parameter pack ([[temp.variadic]](temp.variadic.md)), the template parameter pack will match zero or more template parameters or template parameter packs in the *template-head* of `A` with the same type and form as the template parameter pack in `P` (ignoring whether those template parameters are template parameter packs).

[*Example 2*:

```cpp
template<class T> class A { /* ... */ };
template<class T, class U = T> class B { /* ... */ };
template<class ... Types> class C { /* ... */ };
template<auto n> class D { /* ... */ };
template<template<class> class P> class X { /* ... */ };
template<template<class ...> class Q> class Y { /* ... */ };
template<template<int> class R> class Z { /* ... */ };

X<A> xa;            // OK
X<B> xb;            // OK
X<C> xc;            // OK
Y<A> ya;            // OK
Y<B> yb;            // OK
Y<C> yc;            // OK
Z<D> zd;            // OK
```

— *end example*] [*Example 3*:

```cpp
template <class T> struct eval;

template <template <class, class...> class TT, class T1, class... Rest>
struct eval<TT<T1, Rest...>> { };

template <class T1> struct A;
template <class T1, class T2> struct B;
template <int N> struct C;
template <class T1, int N> struct D;
template <class T1, class T2, int N = 17> struct E;

eval<A<int>> eA;                // OK: matches partial specialization of eval
eval<B<int, float>> eB;         // OK: matches partial specialization of eval
eval<C<17>> eC;                 // error: C does not match TT in partial specialization
eval<D<int, 17>> eD;            // error: D does not match TT in partial specialization
eval<E<int, float>> eE;         // error: E does not match TT in partial specialization
```

— *end example*] [*Example 4*:

```cpp
template<typename T> concept C = requires (T t) { t.f(); };
template<typename T> concept D = C<T> && requires (T t) { t.g(); };

template<template<C> class P> struct S { };

template<C> struct X { };
template<D> struct Y { };
template<typename T> struct Z { };

S<X> s1;            // OK, X and P have equivalent constraints
S<Y> s2;            // error: P is not at least as specialized as Y
S<Z> s3;            // OK, P is at least as specialized as Z
```

— *end example*]

**4** A template *template-parameter* `P` is at least as specialized as a template *template-argument* `A` if, given the following rewrite to two function templates, the function template corresponding to `P` is at least as specialized as the function template corresponding to `A` according to the partial ordering rules for function templates ([[temp.func.order]](temp.func.order.md)). Given an invented class template `X` with the *template-head* of `A` (including default arguments and *requires-clause*, if any):

- Each of the two function templates has the same template parameters and *requires-clause* (if any), respectively, as `P` or `A`.
- Each function template has a single function parameter whose type is a specialization of `X` with template arguments corresponding to the template parameters from the respective function template where, for each template parameter `PP` in the *template-head* of the function template, a corresponding template argument `AA` is formed. If `PP` declares a template parameter pack, then `AA` is the pack expansion `PP...` ([[temp.variadic]](temp.variadic.md)); otherwise, `AA` is the *id-expression* `PP`.

If the rewrite produces an invalid type, then `P` is not at least as specialized as `A`.
