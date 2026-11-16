# 13.8.1 Locally declared names [temp.local]

[Contents](README.md) · up: [13.8 Name resolution \[temp.res\]](temp.res.md) · prev: [13.8 Name resolution \[temp.res\]](temp.res.md) · next: [13.8.2 Dependent names \[temp.dep\]](temp.dep.md)

**1** Like normal (non-template) classes, class templates have an injected-class-name ([[class.pre]](class.pre.md)). The injected-class-name can be used as a *template-name* or a *type-name*. When it is used with a *template-argument-list*, as a *template-argument* for a template *template-parameter*, or as the final identifier in the *elaborated-type-specifier* of a friend class template declaration, it is a *template-name* that refers to the class template itself. Otherwise, it is a *type-name* equivalent to the *template-name* followed by the *template-parameter*s of the class template enclosed in `<>`.

**2** Within the scope of a class template specialization or partial specialization, when the injected-class-name is used as a *type-name*, it is equivalent to the *template-name* followed by the *template-argument*s of the class template specialization or partial specialization enclosed in `<>`. [*Example 1*:

```cpp
template<template<class> class T> class A { };
template<class T> class Y;
template<> class Y<int> {
  Y* p;                                 // meaning Y<int>
  Y<char>* q;                           // meaning Y<char>
  A<Y>* a;                              // meaning A<::Y>
  class B {
    template<class> friend class Y;     // meaning ::Y
  };
};
```

— *end example*]

**3** The injected-class-name of a class template or class template specialization can be used as either a *template-name* or a *type-name* wherever it is in scope. [*Example 2*:

```cpp
template <class T> struct Base {
  Base* p;
};

template <class T> struct Derived: public Base<T> {
  typename Derived::Base* p;            // meaning Derived::Base<T>
};

template<class T, template<class> class U = T::template Base> struct Third { };
Third<Derived<int> > t;                 // OK: default argument uses injected-class-name as a template
```

— *end example*]

**4** A lookup that finds an injected-class-name ([[class.member.lookup]](class.member.lookup.md)) can result in an ambiguity in certain cases (for example, if it is found in more than one base class). If all of the injected-class-names that are found refer to specializations of the same class template, and if the name is used as a *template-name*, the reference refers to the class template itself and not a specialization thereof, and is not ambiguous. [*Example 3*:

```cpp
template <class T> struct Base { };
template <class T> struct Derived: Base<int>, Base<char> {
  typename Derived::Base b;             // error: ambiguous
  typename Derived::Base<double> d;     // OK
};
```

— *end example*]

**5** When the normal name of the template (i.e., the name from the enclosing scope, not the injected-class-name) is used, it always refers to the class template itself and not a specialization of the template. [*Example 4*:

```cpp
template<class T> class X {
  X* p;                                 // meaning X<T>
  X<T>* p2;
  X<int>* p3;
  ::X* p4;                              // error: missing template argument list
                                        // ::X does not refer to the injected-class-name
};
```

— *end example*]

**6** The name of a *template-parameter* shall not be redeclared within its scope (including nested scopes). A *template-parameter* shall not have the same name as the template name. [*Example 5*:

```cpp
template<class T, int i> class Y {
  int T;                                // error: template-parameter redeclared
  void f() {
    char T;                             // error: template-parameter redeclared
  }
};

template<class X> class X;              // error: template-parameter redeclared
```

— *end example*]

**7** In the definition of a member of a class template that appears outside of the class template definition, the name of a member of the class template hides the name of a *template-parameter* of any enclosing class templates (but not a *template-parameter* of the member if the member is a class or function template). [*Example 6*:

```cpp
template<class T> struct A {
  struct B { /* ... */ };
  typedef void C;
  void f();
  template<class U> void g(U);
};

template<class B> void A<B>::f() {
  B b;              // A's B, not the template parameter
}

template<class B> template<class C> void A<B>::g(C) {
  B b;              // A's B, not the template parameter
  C c;              // the template parameter C, not A's C
}
```

— *end example*]

**8** In the definition of a member of a class template that appears outside of the namespace containing the class template definition, the name of a *template-parameter* hides the name of a member of this namespace. [*Example 7*:

```cpp
namespace N {
  class C { };
  template<class T> class B {
    void f(T);
  };
}
template<class C> void N::B<C>::f(C) {
  C b;              // C is the template parameter, not N::C
}
```

— *end example*]

**9** In the definition of a class template or in the definition of a member of such a template that appears outside of the template definition, for each non-dependent base class ([[temp.dep.type]](temp.dep.type.md)), if the name of the base class or the name of a member of the base class is the same as the name of a *template-parameter*, the base class name or member name hides the *template-parameter* name ([[basic.scope.hiding]](basic.scope.hiding.md)). [*Example 8*:

```cpp
struct A {
  struct B { /* ... */ };
  int a;
  int Y;
};

template<class B, class a> struct X : A {
  B b;              // A's B
  a b;              // error: A's a isn't a type name
};
```

— *end example*]
