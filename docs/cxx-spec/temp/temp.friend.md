# 13.7.4 Friends [temp.friend]

[Contents](../README.md) · up: [13.7 Template declarations \[temp.decls\]](temp.decls.md) · prev: [13.7.3 Variadic templates \[temp.variadic\]](temp.variadic.md) · next: [13.7.5 Class template partial specializations \[temp.class.spec\]](temp.class.spec.md)

**1** A friend of a class or class template can be a function template or class template, a specialization of a function template or class template, or a non-template function or class. For a friend function declaration that is not a template declaration:

- if the name of the friend is a qualified or unqualified *template-id*, the friend declaration refers to a specialization of a function template, otherwise,
- if the name of the friend is a *qualified-id* and a matching non-template function is found in the specified class or namespace, the friend declaration refers to that function, otherwise,
- if the name of the friend is a *qualified-id* and a matching function template is found in the specified class or namespace, the friend declaration refers to the deduced specialization of that function template ([[temp.deduct.decl]](temp.deduct.decl.md)), otherwise,
- the name shall be an *unqualified-id* that declares (or redeclares) a non-template function.

[*Example 1*:

```cpp
template<class T> class task;
template<class T> task<T>* preempt(task<T>*);

template<class T> class task {
  friend void next_time();
  friend void process(task<T>*);
  friend task<T>* preempt<T>(task<T>*);
  template<class C> friend int func(C);

  friend class task<int>;
  template<class P> friend class frd;
};
```

Here, each specialization of the `task` class template has the function `next_time` as a friend; because `process` does not have explicit *template-argument*s, each specialization of the `task` class template has an appropriately typed function `process` as a friend, and this friend is not a function template specialization; because the friend `preempt` has an explicit *template-argument* `T`, each specialization of the `task` class template has the appropriate specialization of the function template `preempt` as a friend; and each specialization of the `task` class template has all specializations of the function template `func` as friends. Similarly, each specialization of the `task` class template has the class template specialization `task<int>` as a friend, and has all specializations of the class template `frd` as friends. — *end example*]

**2** A friend template may be declared within a class or class template. A friend function template may be defined within a class or class template, but a friend class template may not be defined in a class or class template. In these cases, all specializations of the friend class or friend function template are friends of the class or class template granting friendship. [*Example 2*:

```cpp
class A {
  template<class T> friend class B;                 // OK
  template<class T> friend void f(T){ /* ... */ }   // OK
};
```

— *end example*]

**3** A template friend declaration specifies that all specializations of that template, whether they are implicitly instantiated ([[temp.inst]](temp.inst.md)), partially specialized ([[temp.class.spec]](temp.class.spec.md)) or explicitly specialized ([[temp.expl.spec]](temp.expl.spec.md)), are friends of the class containing the template friend declaration. [*Example 3*:

```cpp
class X {
  template<class T> friend struct A;
  class Y { };
};

template<class T> struct A { X::Y ab; };            // OK
template<class T> struct A<T*> { X::Y ab; };        // OK
```

— *end example*]

**4** A template friend declaration may declare a member of a dependent type to be a friend. The friend declaration shall declare a function or specify a type with an *elaborated-type-specifier*, in either case with a *nested-name-specifier* ending with a *simple-template-id*, *C*, whose *template-name* names a class template. The template parameters of the template friend declaration shall be deducible from *C* ([[temp.deduct.type]](temp.deduct.type.md)). In this case, a member of a specialization *S* of the class template is a friend of the class granting friendship if deduction of the template parameters of *C* from *S* succeeds, and substituting the deduced template arguments into the friend declaration produces a declaration that would be a valid redeclaration of the member of the specialization. [*Example 4*:

```cpp
template<class T> struct A {
  struct B { };
  void f();
  struct D {
    void g();
  };
  T h();
  template<T U> T i();
};
template<> struct A<int> {
  struct B { };
  int f();
  struct D {
    void g();
  };
  template<int U> int i();
};
template<> struct A<float*> {
  int *h();
};

class C {
  template<class T> friend struct A<T>::B;      // grants friendship to A<int>::B even though
                                                // it is not a specialization of A<T>::B
  template<class T> friend void A<T>::f();      // does not grant friendship to A<int>::f()
                                                // because its return type does not match
  template<class T> friend void A<T>::D::g();   // error: A<T>::D does not end with a simple-template-id
  template<class T> friend int *A<T*>::h();     // grants friendship to A<int*>::h() and A<float*>::h()
  template<class T> template<T U>               // grants friendship to instantiations of A<T>::i() and
    friend T A<T>::i();                         // to A<int>::i(), and thereby to all specializations
};                                              // of those function templates
```

— *end example*]

**5** [*Note 1*: A friend declaration may first declare a member of an enclosing namespace scope ([[temp.inject]](temp.inject.md)). — *end note*]

**6** A friend template shall not be declared in a local class.

**7** Friend declarations shall not declare partial specializations. [*Example 5*:

```cpp
template<class T> class A { };
class X {
  template<class T> friend class A<T*>;         // error
};
```

— *end example*]

**8** When a friend declaration refers to a specialization of a function template, the function parameter declarations shall not include default arguments, nor shall the `inline`, `constexpr`, or `consteval` specifiers be used in such a declaration.

**9** A non-template friend declaration with a *requires-clause* shall be a definition. A friend function template with a constraint that depends on a template parameter from an enclosing template shall be a definition. Such a constrained friend function or function template declaration does not declare the same function or function template as a declaration in any other scope.
