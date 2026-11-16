# 13.7.7 Alias templates [temp.alias]

[Contents](../README.md) · up: [13.7 Template declarations \[temp.decls\]](temp.decls.md) · prev: [13.7.6.2 Partial ordering of function templates \[temp.func.order\]](temp.func.order.md) · next: [13.7.8 Concept definitions \[temp.concept\]](temp.concept.md)

**1** A *template-declaration* in which the *declaration* is an *alias-declaration* ([[dcl.pre]](../dcl/dcl.pre.md)) declares the *identifier* to be an *alias template*. An alias template is a name for a family of types. The name of the alias template is a *template-name*.

**2** When a *template-id* refers to the specialization of an alias template, it is equivalent to the associated type obtained by substitution of its *template-argument*s for the *template-parameter*s in the *defining-type-id* of the alias template. [*Note 1*: An alias template name is never deduced. — *end note*] [*Example 1*:

```cpp
template<class T> struct Alloc { /* ... */ };
template<class T> using Vec = vector<T, Alloc<T>>;
Vec<int> v;         // same as vector<int, Alloc<int>> v;

template<class T>
  void process(Vec<T>& v)
  { /* ... */ }

template<class T>
  void process(vector<T, Alloc<T>>& w)
  { /* ... */ }     // error: redefinition

template<template<class> class TT>
  void f(TT<int>);

f(v);               // error: Vec not deduced

template<template<class,class> class TT>
  void g(TT<int, Alloc<int>>);
g(v);               // OK: TT = vector
```

— *end example*]

**3** However, if the *template-id* is dependent, subsequent template argument substitution still applies to the *template-id*. [*Example 2*:

```cpp
template<typename...> using void_t = void;
template<typename T> void_t<typename T::foo> f();
f<int>();           // error: int does not have a nested type foo
```

— *end example*]

**4** The *defining-type-id* in an alias template declaration shall not refer to the alias template being declared. The type produced by an alias template specialization shall not directly or indirectly make use of that specialization. [*Example 3*:

```cpp
template <class T> struct A;
template <class T> using B = typename A<T>::U;
template <class T> struct A {
  typedef B<T> U;
};
B<short> b;         // error: instantiation of B<short> uses own type via A<short>::U
```

— *end example*]

**5** The type of a *lambda-expression* appearing in an alias template declaration is different between instantiations of that template, even when the *lambda-expression* is not dependent. [*Example 4*:

```cpp
template <class T>
  using A = decltype([] { });   // A<int> and A<char> refer to different closure types
```

— *end example*]
