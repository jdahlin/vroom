# 9.2.8.4 Decltype specifiers [dcl.type.decltype]

[Contents](../README.md) · up: [9.2.8 Type specifiers \[dcl.type\]](dcl.type.md) · prev: [9.2.8.3 Elaborated type specifiers \[dcl.type.elab\]](dcl.type.elab.md) · next: [9.2.8.5 Placeholder type specifiers \[dcl.spec.auto\]](dcl.spec.auto.md)

```bnf
decltype-specifier:
  'decltype' '(' expression ')'
```

**1** For an expression *E*, the type denoted by `decltype(E)` is defined as follows:

- if *E* is an unparenthesized *id-expression* naming a structured binding ([[dcl.struct.bind]](dcl.struct.bind.md)), `decltype(E)` is the referenced type as given in the specification of the structured binding declaration;
- otherwise, if *E* is an unparenthesized *id-expression* naming a non-type *template-parameter* ([[temp.param]](../temp/temp.param.md)), `decltype(E)` is the type of the *template-parameter* after performing any necessary type deduction ([[dcl.spec.auto]](dcl.spec.auto.md), [[dcl.type.class.deduct]](dcl.type.class.deduct.md));
- otherwise, if *E* is an unparenthesized *id-expression* or an unparenthesized class member access ([[expr.ref]](../expr/expr.ref.md)), `decltype(E)` is the type of the entity named by *E*. If there is no such entity, or if *E* names a set of overloaded functions, the program is ill-formed;
- otherwise, if *E* is an xvalue, `decltype(E)` is `T&&`, where `T` is the type of *E*;
- otherwise, if *E* is an lvalue, `decltype(E)` is `T&`, where `T` is the type of *E*;
- otherwise, `decltype(E)` is the type of *E*.

The operand of the `decltype` specifier is an unevaluated operand ([[expr.prop]](../expr/expr.prop.md)).

[*Example 1*:

```cpp
const int&& foo();
int i;
struct A { double x; };
const A* a = new A();
decltype(foo()) x1 = 17;        // type is const int&&
decltype(i) x2;                 // type is int
decltype(a->x) x3;              // type is double
decltype((a->x)) x4 = x3;       // type is const double&
```

— *end example*] [*Note 1*: The rules for determining types involving `decltype(auto)` are specified in [[dcl.spec.auto]](dcl.spec.auto.md). — *end note*]

**2** If the operand of a *decltype-specifier* is a prvalue and is not a (possibly parenthesized) immediate invocation ([[expr.const]](../expr/expr.const.md)), the temporary materialization conversion is not applied ([[conv.rval]](../expr/conv.rval.md)) and no result object is provided for the prvalue. The type of the prvalue may be incomplete or an abstract class type. [*Note 2*: As a result, storage is not allocated for the prvalue and it is not destroyed. Thus, a class type is not instantiated as a result of being the type of a function call in this context. In this context, the common purpose of writing the expression is merely to refer to its type. In that sense, a *decltype-specifier* is analogous to a use of a *typedef-name*, so the usual reasons for requiring a complete type do not apply. In particular, it is not necessary to allocate storage for a temporary object or to enforce the semantic constraints associated with invoking the type’s destructor. — *end note*] [*Note 3*: Unlike the preceding rule, parentheses have no special meaning in this context. — *end note*] [*Example 2*:

```cpp
template<class T> struct A { ~A() = delete; };
template<class T> auto h()
  -> A<T>;
template<class T> auto i(T)     // identity
  -> T;
template<class T> auto f(T)     // #1
  -> decltype(i(h<T>()));       // forces completion of A<T> and implicitly uses A<T>::~A()
                                // for the temporary introduced by the use of h().
                                // (A temporary is not introduced as a result of the use of i().)
template<class T> auto f(T)     // #2
  -> void;
auto g() -> void {
  f(42);                        // OK: calls #2. (#1 is not a viable candidate: type deduction
                                // fails ([temp.deduct]) because A<int>::~A() is implicitly used in its
                                // decltype-specifier)
}
template<class T> auto q(T)
  -> decltype((h<T>()));        // does not force completion of A<T>; A<T>::~A() is not implicitly
                                // used within the context of this decltype-specifier
void r() {
  q(42);                        // error: deduction against q succeeds, so overload resolution selects
                                // the specialization ``q(T) -> decltype((h<T>()))'' with T=int;
                                // the return type is A<int>, so a temporary is introduced and its
                                // destructor is used, so the program is ill-formed
}
```

— *end example*]
