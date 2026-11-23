# 13.9.1 Implicit instantiation [temp.inst]

[Contents](../README.md) · up: [13.9 Template instantiation and specialization \[temp.spec\]](temp.spec.md) · prev: [13.9 Template instantiation and specialization \[temp.spec\]](temp.spec.md) · next: [13.9.2 Explicit instantiation \[temp.explicit\]](temp.explicit.md)

**1** A template specialization *E* is a *declared specialization* if there is a reachable explicit instantiation definition ([[temp.explicit]](temp.explicit.md)) or explicit specialization declaration ([[temp.expl.spec]](temp.expl.spec.md)) for *E*, or if there is a reachable explicit instantiation declaration for *E* and *E* is not

- an inline function,
- declared with a type deduced from its initializer or return value ([[dcl.spec.auto]](../dcl/dcl.spec.auto.md)),
- a potentially-constant variable ([[expr.const]](../expr/expr.const.md)), or
- a specialization of a templated class.

[*Note 1*: An implicit instantiation in an importing translation unit cannot use names with internal linkage from an imported translation unit ([[basic.link]](../basic/basic.link.md)). — *end note*]

**2** Unless a class template specialization is a declared specialization, the class template specialization is implicitly instantiated when the specialization is referenced in a context that requires a completely-defined object type or when the completeness of the class type affects the semantics of the program. [*Note 2*: In particular, if the semantics of an expression depend on the member or base class lists of a class template specialization, the class template specialization is implicitly generated. For instance, deleting a pointer to class type depends on whether or not the class declares a destructor, and a conversion between pointers to class type depends on the inheritance relationship between the two classes involved. — *end note*] [*Example 1*:

```cpp
template<class T> class B { /* ... */ };
template<class T> class D : public B<T> { /* ... */ };

void f(void*);
void f(B<int>*);

void g(D<int>* p, D<char>* pp, D<double>* ppp) {
  f(p);             // instantiation of D<int> required: call f(B<int>*)
  B<char>* q = pp;  // instantiation of D<char> required: convert D<char>* to B<char>*
  delete ppp;       // instantiation of D<double> required
}
```

— *end example*] If a class template has been declared, but not defined, at the point of instantiation ([[temp.point]](temp.point.md)), the instantiation yields an incomplete class type ([[basic.types]](../basic/basic.types.md)). [*Example 2*:

```cpp
template<class T> class X;
X<char> ch;         // error: incomplete type X<char>
```

— *end example*] [*Note 3*: Within a template declaration, a local class ([[class.local]](../class/class.local.md)) or enumeration and the members of a local class are never considered to be entities that can be separately instantiated (this includes their default arguments, *noexcept-specifier*s, and non-static data member initializers, if any, but not their *type-constraint*s or *requires-clause*s). As a result, the dependent names are looked up, the semantic constraints are checked, and any templates used are instantiated as part of the instantiation of the entity within which the local class or enumeration is declared. — *end note*]

**3** The implicit instantiation of a class template specialization causes

- the implicit instantiation of the declarations, but not of the definitions, of the non-deleted class member functions, member classes, scoped member enumerations, static data members, member templates, and friends; and
- the implicit instantiation of the definitions of deleted member functions, unscoped member enumerations, and member anonymous unions.

The implicit instantiation of a class template specialization does not cause the implicit instantiation of default arguments or *noexcept-specifier*s of the class member functions. [*Example 3*:

```cpp
template<class T>
struct C {
  void f() { T x; }
  void g() = delete;
};
C<void> c;                      // OK, definition of C<void>::f is not instantiated at this point
template<> void C<int>::g() { } // error: redefinition of C<int>::g
```

— *end example*] However, for the purpose of determining whether an instantiated redeclaration is valid according to [[basic.def.odr]](../basic/basic.def.odr.md) and [[class.mem]](../class/class.mem.md), a declaration that corresponds to a definition in the template is considered to be a definition. [*Example 4*:

```cpp
template<class T, class U>
struct Outer {
  template<class X, class Y> struct Inner;
  template<class Y> struct Inner<T, Y>;         // #1a
  template<class Y> struct Inner<T, Y> { };     // #1b; OK: valid redeclaration of #1a
  template<class Y> struct Inner<U, Y> { };     // #2
};

Outer<int, int> outer;                          // error at #2
```

`Outer<int, int>::Inner<int, Y>` is redeclared at #1b. (It is not defined but noted as being associated with a definition in `Outer<T, U>`.) #2 is also a redeclaration of #1a. It is noted as associated with a definition, so it is an invalid redeclaration of the same partial specialization.

```cpp
template<typename T> struct Friendly {
  template<typename U> friend int f(U) { return sizeof(T); }
};
Friendly<char> fc;
Friendly<float> ff;                             // error: produces second definition of f(U)
```

— *end example*]

**4** Unless a member of a class template or a member template is a declared specialization, the specialization of the member is implicitly instantiated when the specialization is referenced in a context that requires the member definition to exist or if the existence of the definition of the member affects the semantics of the program; in particular, the initialization (and any associated side effects) of a static data member does not occur unless the static data member is itself used in a way that requires the definition of the static data member to exist.

**5** Unless a function template specialization is a declared specialization, the function template specialization is implicitly instantiated when the specialization is referenced in a context that requires a function definition to exist or if the existence of the definition affects the semantics of the program. A function whose declaration was instantiated from a friend function definition is implicitly instantiated when it is referenced in a context that requires a function definition to exist or if the existence of the definition affects the semantics of the program. Unless a call is to a function template explicit specialization or to a member function of an explicitly specialized class template, a default argument for a function template or a member function of a class template is implicitly instantiated when the function is called in a context that requires the value of the default argument. [*Note 4*: An inline function that is the subject of an explicit instantiation declaration is not a declared specialization; the intent is that it still be implicitly instantiated when odr-used ([[basic.def.odr]](../basic/basic.def.odr.md)) so that the body can be considered for inlining, but that no out-of-line copy of it be generated in the translation unit. — *end note*]

**6** [*Example 5*:

```cpp
template<class T> struct Z {
  void f();
  void g();
};

void h() {
  Z<int> a;         // instantiation of class Z<int> required
  Z<char>* p;       // instantiation of class Z<char> not required
  Z<double>* q;     // instantiation of class Z<double> not required

  a.f();            // instantiation of Z<int>::f() required
  p->g();           // instantiation of class Z<char> required, and
                    // instantiation of Z<char>::g() required
}
```

Nothing in this example requires `class` `Z<double>`, `Z<int>::g()`, or `Z<char>::f()` to be implicitly instantiated. — *end example*]

**7** Unless a variable template specialization is a declared specialization, the variable template specialization is implicitly instantiated when it is referenced in a context that requires a variable definition to exist or if the existence of the definition affects the semantics of the program. A default template argument for a variable template is implicitly instantiated when the variable template is referenced in a context that requires the value of the default argument.

**8** The existence of a definition of a variable or function is considered to affect the semantics of the program if the variable or function is needed for constant evaluation by an expression ([[expr.const]](../expr/expr.const.md)), even if constant evaluation of the expression is not required or if constant expression evaluation does not use the definition.

[*Example 6*:

```cpp
template<typename T> constexpr int f() { return T::value; }
template<bool B, typename T> void g(decltype(B ? f<T>() : 0));
template<bool B, typename T> void g(...);
template<bool B, typename T> void h(decltype(int{B ? f<T>() : 0}));
template<bool B, typename T> void h(...);
void x() {
  g<false, int>(0); // OK, B ? f<T>(): 0 is not potentially constant evaluated
  h<false, int>(0); // error, instantiates f<int> even though B evaluates to false and
                    // list-initialization of int from int cannot be narrowing
}
```

— *end example*]

**9** If the function selected by overload resolution ([[over.match]](../over/over.match.md)) can be determined without instantiating a class template definition, it is unspecified whether that instantiation actually takes place. [*Example 7*:

```cpp
template <class T> struct S {
  operator int();
};

void f(int);
void f(S<int>&);
void f(S<float>);

void g(S<int>& sr) {
  f(sr);            // instantiation of S<int> allowed but not required
                    // instantiation of S<float> allowed but not required
};
```

— *end example*]

**10** If a function template or a member function template specialization is used in a way that involves overload resolution, a declaration of the specialization is implicitly instantiated ([[temp.over]](temp.over.md)).

**11** An implementation shall not implicitly instantiate a function template, a variable template, a member template, a non-virtual member function, a member class, a static data member of a class template, or a substatement of a constexpr if statement ([[stmt.if]](../stmt/stmt.if.md)), unless such instantiation is required. [*Note 5*: The instantiation of a generic lambda does not require instantiation of substatements of a constexpr if statement within its *compound-statement* unless the call operator template is instantiated. — *end note*] It is unspecified whether or not an implementation implicitly instantiates a virtual member function of a class template if the virtual member function would not otherwise be instantiated. The use of a template specialization in a default argument shall not cause the template to be implicitly instantiated except that a class template may be instantiated where its complete type is needed to determine the correctness of the default argument. The use of a default argument in a function call causes specializations in the default argument to be implicitly instantiated.

**12** Implicitly instantiated class, function, and variable template specializations are placed in the namespace where the template is defined. Implicitly instantiated specializations for members of a class template are placed in the namespace where the enclosing class template is defined. Implicitly instantiated member templates are placed in the namespace where the enclosing class or class template is defined. [*Example 8*:

```cpp
namespace N {
  template<class T> class List {
  public:
    T* get();
  };
}

template<class K, class V> class Map {
public:
  N::List<V> lt;
  V get(K);
};

void g(Map<const char*,int>& m) {
  int i = m.get("Nicholas");
}
```

A call of `lt.get()` from `Map<const char*,int>::get()` would place `List<int>::get()` in the namespace `N` rather than in the global namespace. — *end example*]

**13** If a function template `f` is called in a way that requires a default argument to be used, the dependent names are looked up, the semantics constraints are checked, and the instantiation of any template used in the default argument is done as if the default argument had been an initializer used in a function template specialization with the same scope, the same template parameters and the same access as that of the function template `f` used at that point, except that the scope in which a closure type is declared ([[expr.prim.lambda.closure]](../expr/expr.prim.lambda.closure.md)) – and therefore its associated namespaces – remain as determined from the context of the definition for the default argument. This analysis is called *default argument instantiation*. The instantiated default argument is then used as the argument of `f`.

**14** Each default argument is instantiated independently. [*Example 9*:

```cpp
template<class T> void f(T x, T y = ydef(T()), T z = zdef(T()));

class  A { };

A zdef(A);

void g(A a, A b, A c) {
  f(a, b, c);       // no default argument instantiation
  f(a, b);          // default argument z = zdef(T()) instantiated
  f(a);             // error: ydef is not declared
}
```

— *end example*]

**15** The *noexcept-specifier* of a function template specialization is not instantiated along with the function declaration; it is instantiated when needed ([[except.spec]](../except/except.spec.md)). If such an *noexcept-specifier* is needed but has not yet been instantiated, the dependent names are looked up, the semantics constraints are checked, and the instantiation of any template used in the *noexcept-specifier* is done as if it were being done as part of instantiating the declaration of the specialization at that point.

**16** [*Note 6*: [[temp.point]](temp.point.md) defines the point of instantiation of a template specialization. — *end note*]

**17** There is an implementation-defined quantity that specifies the limit on the total depth of recursive instantiations ([[implimits]](../implimits/implimits.md)), which could involve more than one template. The result of an infinite recursion in instantiation is undefined. [*Example 10*:

```cpp
template<class T> class X {
  X<T>* p;          // OK
  X<T*> a;          // implicit generation of X<T> requires
                    // the implicit instantiation of X<T*> which requires
                    // the implicit instantiation of X<T**> which ...
};
```

— *end example*]

**18** The *type-constraint*s and *requires-clause* of a template specialization or member function are not instantiated along with the specialization or function itself, even for a member function of a local class; substitution into the atomic constraints formed from them is instead performed as specified in [[temp.constr.decl]](temp.constr.decl.md) and [[temp.constr.atomic]](temp.constr.atomic.md) when determining whether the constraints are satisfied or as specified in [[temp.constr.decl]](temp.constr.decl.md) when comparing declarations. [*Note 7*: The satisfaction of constraints is determined during template argument deduction ([[temp.deduct]](temp.deduct.md)) and overload resolution ([[over.match]](../over/over.match.md)). — *end note*] [*Example 11*:

```cpp
template<typename T> concept C = sizeof(T) > 2;
template<typename T> concept D = C<T> && sizeof(T) > 4;

template<typename T> struct S {
  S() requires C<T> { }         // #1
  S() requires D<T> { }         // #2
};

S<char> s1;                     // error: no matching constructor
S<char[8]> s2;                  // OK, calls #2
```

When `S<char>` is instantiated, both constructors are part of the specialization. Their constraints are not satisfied, and they suppress the implicit declaration of a default constructor for `S<char>` ([[class.default.ctor]](../class/class.default.ctor.md)), so there is no viable constructor for `s1`. — *end example*] [*Example 12*:

```cpp
template<typename T> struct S1 {
  template<typename U>
    requires false
  struct Inner1;                // ill-formed, no diagnostic required
};

template<typename T> struct S2 {
  template<typename U>
    requires (sizeof(T[-(int)sizeof(T)]) > 1)
  struct Inner2;                // ill-formed, no diagnostic required
};
```

The class `S1<T>::Inner1` is ill-formed, no diagnostic required, because it has no valid specializations. `S2` is ill-formed, no diagnostic required, since no substitution into the constraints of its `Inner2` template would result in a valid expression. — *end example*]
