# 13.8.2 Dependent names [temp.dep]

[Contents](../README.md) · up: [13.8 Name resolution \[temp.res\]](temp.res.md) · prev: [13.8.1 Locally declared names \[temp.local\]](temp.local.md) · next: [13.8.2.1 Dependent types \[temp.dep.type\]](temp.dep.type.md)

**1** Inside a template, some constructs have semantics which may differ from one instantiation to another. Such a construct *depends* on the template parameters. In particular, types and expressions may depend on the type and/or value of template parameters (as determined by the template arguments) and this determines the context for name lookup for certain names. An expression may be *type-dependent* (that is, its type may depend on a template parameter) or *value-dependent* (that is, its value when evaluated as a constant expression ([[expr.const]](../expr/expr.const.md)) may depend on a template parameter) as described in this subclause.

**2** In an expression of the form:

```bnf
postfix-expression '(' expression-listₒₚₜ ')'
```

where the *postfix-expression* is an *unqualified-id*, the *unqualified-id* denotes a *dependent name* if

- any of the expressions in the *expression-list* is a pack expansion ([[temp.variadic]](temp.variadic.md)),
- any of the expressions or *braced-init-list*s in the *expression-list* is type-dependent ([[temp.dep.expr]](temp.dep.expr.md)), or
- the *unqualified-id* is a *template-id* in which any of the template arguments depends on a template parameter.

If an operand of an operator is a type-dependent expression, the operator also denotes a dependent name. [*Note 1*: Such names are unbound and are looked up at the point of the template instantiation ([[temp.point]](temp.point.md)) in both the context of the template definition and the context of the point of instantiation ([[temp.dep.candidate]](temp.dep.candidate.md)). — *end note*]

**3** [*Example 1*:

```cpp
template<class T> struct X : B<T> {
  typename T::A* pa;
  void f(B<T>* pb) {
    static int i = B<T>::i;
    pb->j++;
  }
};
```

The base class name `B<T>`, the type name `T::A`, the names `B<T>::i` and `pb->j` explicitly depend on the *template-parameter*. — *end example*]

**4** In the definition of a class or class template, the scope of a dependent base class ([[temp.dep.type]](temp.dep.type.md)) is not examined during unqualified name lookup either at the point of definition of the class template or member or during an instantiation of the class template or member. [*Example 2*:

```cpp
typedef double A;
template<class T> class B {
  typedef int A;
};
template<class T> struct X : B<T> {
  A a;              // a has type double
};
```

The type name `A` in the definition of `X<T>` binds to the typedef name defined in the global namespace scope, not to the typedef name defined in the base class `B<T>`. — *end example*] [*Example 3*:

```cpp
struct A {
  struct B { /* ... */ };
  int a;
  int Y;
};

int a;

template<class T> struct Y : T {
  struct B { /* ... */ };
  B b;                          // The B defined in Y
  void f(int i) { a = i; }      // ::a
  Y* p;                         // Y<T>
};

Y<A> ya;
```

The members `A::B`, `A::a`, and `A::Y` of the template argument `A` do not affect the binding of names in `Y<A>`. — *end example*]

## Subclauses

- [13.8.2.1 Dependent types \[temp.dep.type\]](temp.dep.type.md)
- [13.8.2.2 Type-dependent expressions \[temp.dep.expr\]](temp.dep.expr.md)
- [13.8.2.3 Value-dependent expressions \[temp.dep.constexpr\]](temp.dep.constexpr.md)
- [13.8.2.4 Dependent template arguments \[temp.dep.temp\]](temp.dep.temp.md)
