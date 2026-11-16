# 13.8.2.1 Dependent types [temp.dep.type]

[Contents](README.md) · up: [13.8.2 Dependent names \[temp.dep\]](temp.dep.md) · prev: [13.8.2 Dependent names \[temp.dep\]](temp.dep.md) · next: [13.8.2.2 Type-dependent expressions \[temp.dep.expr\]](temp.dep.expr.md)

**1** A name refers to the *current instantiation* if it is

- in the definition of a class template, a nested class of a class template, a member of a class template, or a member of a nested class of a class template, the injected-class-name ([[class.pre]](class.pre.md)) of the class template or nested class,
- in the definition of a primary class template or a member of a primary class template, the name of the class template followed by the template argument list of the primary template (as described below) enclosed in `<>` (or an equivalent template alias specialization),
- in the definition of a nested class of a class template, the name of the nested class referenced as a member of the current instantiation, or
- in the definition of a partial specialization or a member of a partial specialization, the name of the class template followed by the template argument list of the partial specialization enclosed in `<>` (or an equivalent template alias specialization). If the *n*<sup></sup>*th* template parameter is a template parameter pack, the *n*<sup></sup>*th* template argument is a pack expansion ([[temp.variadic]](temp.variadic.md)) whose pattern is the name of the template parameter pack.

**2** The template argument list of a primary template is a template argument list in which the *n*<sup></sup>*th* template argument has the value of the *n*<sup></sup>*th* template parameter of the class template. If the *n*<sup></sup>*th* template parameter is a template parameter pack ([[temp.variadic]](temp.variadic.md)), the *n*<sup></sup>*th* template argument is a pack expansion ([[temp.variadic]](temp.variadic.md)) whose pattern is the name of the template parameter pack.

**3** A template argument that is equivalent to a template parameter can be used in place of that template parameter in a reference to the current instantiation. For a template *type-parameter*, a template argument is equivalent to a template parameter if it denotes the same type. For a non-type template parameter, a template argument is equivalent to a template parameter if it is an *identifier* that names a variable that is equivalent to the template parameter. A variable is equivalent to a template parameter if

- it has the same type as the template parameter (ignoring cv-qualification) and
- its initializer consists of a single *identifier* that names the template parameter or, recursively, such a variable.

[*Note 1*: Using a parenthesized variable name breaks the equivalence. — *end note*] [*Example 1*:

```cpp
template <class T> class A {
  A* p1;                        // A is the current instantiation
  A<T>* p2;                     // A<T> is the current instantiation
  A<T*> p3;                     // A<T*> is not the current instantiation
  ::A<T>* p4;                   // ::A<T> is the current instantiation
  class B {
    B* p1;                      // B is the current instantiation
    A<T>::B* p2;                // A<T>::B is the current instantiation
    typename A<T*>::B* p3;      // A<T*>::B is not the current instantiation
  };
};

template <class T> class A<T*> {
  A<T*>* p1;                    // A<T*> is the current instantiation
  A<T>* p2;                     // A<T> is not the current instantiation
};

template <class T1, class T2, int I> struct B {
  B<T1, T2, I>* b1;             // refers to the current instantiation
  B<T2, T1, I>* b2;             // not the current instantiation
  typedef T1 my_T1;
  static const int my_I = I;
  static const int my_I2 = I+0;
  static const int my_I3 = my_I;
  static const long my_I4 = I;
  static const int my_I5 = (I);
  B<my_T1, T2, my_I>* b3;       // refers to the current instantiation
  B<my_T1, T2, my_I2>* b4;      // not the current instantiation
  B<my_T1, T2, my_I3>* b5;      // refers to the current instantiation
  B<my_T1, T2, my_I4>* b6;      // not the current instantiation
  B<my_T1, T2, my_I5>* b7;      // not the current instantiation
};
```

— *end example*]

**4** A *dependent base class* is a base class that is a dependent type and is not the current instantiation. [*Note 2*: A base class can be the current instantiation in the case of a nested class naming an enclosing class as a base. [*Example 2*:

```cpp
template<class T> struct A {
  typedef int M;
  struct B {
    typedef void M;
    struct C;
  };
};

template<class T> struct A<T>::B::C : A<T> {
  M m;                          // OK, A<T>::M
};
```

— *end example*] — *end note*]

**5** A name is a *member of the current instantiation* if it is

- An unqualified name that, when looked up, refers to at least one member of a class that is the current instantiation or a non-dependent base class thereof. [*Note 3*: This can only occur when looking up a name in a scope enclosed by the definition of a class template. — *end note*]
- A *qualified-id* in which the *nested-name-specifier* refers to the current instantiation and that, when looked up, refers to at least one member of a class that is the current instantiation or a non-dependent base class thereof. [*Note 4*: If no such member is found, and the current instantiation has any dependent base classes, then the *qualified-id* is a member of an unknown specialization; see below. — *end note*]
- An *id-expression* denoting the member in a class member access expression ([[expr.ref]](expr.ref.md)) for which the type of the object expression is the current instantiation, and the *id-expression*, when looked up ([[basic.lookup.classref]](basic.lookup.classref.md)), refers to at least one member of a class that is the current instantiation or a non-dependent base class thereof. [*Note 5*: If no such member is found, and the current instantiation has any dependent base classes, then the *id-expression* is a member of an unknown specialization; see below. — *end note*]

[*Example 3*:

```cpp
template <class T> class A {
  static const int i = 5;
  int n1[i];                    // i refers to a member of the current instantiation
  int n2[A::i];                 // A::i refers to a member of the current instantiation
  int n3[A<T>::i];              // A<T>::i refers to a member of the current instantiation
  int f();
};

template <class T> int A<T>::f() {
  return i;                     // i refers to a member of the current instantiation
}
```

— *end example*]

A name is a *dependent member of the current instantiation* if it is a member of the current instantiation that, when looked up, refers to at least one member of a class that is the current instantiation.

**6** A name is a *member of an unknown specialization* if it is

- A *qualified-id* in which the *nested-name-specifier* names a dependent type that is not the current instantiation.

- A *qualified-id* in which the *nested-name-specifier* refers to the current instantiation, the current instantiation has at least one dependent base class, and name lookup of the *qualified-id* does not find any member of a class that is the current instantiation or a non-dependent base class thereof.

- An *id-expression* denoting the member in a class member access expression ([[expr.ref]](expr.ref.md)) in which either

  - the type of the object expression is the current instantiation, the current instantiation has at least one dependent base class, and name lookup of the *id-expression* does not find a member of a class that is the current instantiation or a non-dependent base class thereof; or
  - the type of the object expression is not the current instantiation and the object expression is type-dependent.

**7** If a *qualified-id* in which the *nested-name-specifier* refers to the current instantiation is not a member of the current instantiation or a member of an unknown specialization, the program is ill-formed even if the template containing the *qualified-id* is not instantiated; no diagnostic required. Similarly, if the *id-expression* in a class member access expression for which the type of the object expression is the current instantiation does not refer to a member of the current instantiation or a member of an unknown specialization, the program is ill-formed even if the template containing the member access expression is not instantiated; no diagnostic required. [*Example 4*:

```cpp
template<class T> class A {
  typedef int type;
  void f() {
    A<T>::type i;               // OK: refers to a member of the current instantiation
    typename A<T>::other j;     // error: neither a member of the current instantiation nor
                                // a member of an unknown specialization
  }
};
```

— *end example*]

**8** If, for a given set of template arguments, a specialization of a template is instantiated that refers to a member of the current instantiation with a *qualified-id* or class member access expression, the name in the *qualified-id* or class member access expression is looked up in the template instantiation context. If the result of this lookup differs from the result of name lookup in the template definition context, name lookup is ambiguous. [*Example 5*:

```cpp
struct A {
  int m;
};

struct B {
  int m;
};

template<typename T>
struct C : A, T {
  int f() { return this->m; }   // finds A::m in the template definition context
  int g() { return m; }         // finds A::m in the template definition context
};

template int C<B>::f();         // error: finds both A::m and B::m
template int C<B>::g();         // OK: transformation to class member access syntax
                                // does not occur in the template definition context; see [class.mfct.non-static]
```

— *end example*]

**9** A type is dependent if it is

- a template parameter,
- a member of an unknown specialization,
- a nested class or enumeration that is a dependent member of the current instantiation,
- a cv-qualified type where the cv-unqualified type is dependent,
- a compound type constructed from any dependent type,
- an array type whose element type is dependent or whose bound (if any) is value-dependent,
- a function type whose exception specification is value-dependent,
- denoted by a *simple-template-id* in which either the template name is a template parameter or any of the template arguments is a dependent type or an expression that is type-dependent or value-dependent or is a pack expansion [*Note 6*: This includes an injected-class-name ([[class.pre]](class.pre.md)) of a class template used without a *template-argument-list*. — *end note*], or
- denoted by `decltype(`*expression*`)`, where *expression* is type-dependent ([[temp.dep.expr]](temp.dep.expr.md)).

**10** [*Note 7*: Because typedefs do not introduce new types, but instead simply refer to other types, a name that refers to a typedef that is a member of the current instantiation is dependent only if the type referred to is dependent. — *end note*]
