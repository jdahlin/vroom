# 18.5.2 Boolean testability [concept.booleantestable]

[Contents](../README.md) · up: [18.5 Comparison concepts \[concepts.compare\]](concepts.compare.md) · prev: [18.5.1 General \[concepts.compare.general\]](concepts.compare.general.md) · next: [18.5.3 Concept `equality_comparable` \[concept.equalitycomparable\]](concept.equalitycomparable.md)

**1** The exposition-only `boolean-testable` concept specifies the requirements on expressions that are convertible to `bool` and for which the logical operators ([[expr.log.and]](../expr.log.and.md), [[expr.log.or]](../expr.log.or.md), [[expr.unary.op]](../expr.unary.op.md)) have the conventional semantics.

```cpp
template<class T>
  concept boolean-testable-impl = convertible_to<T, bool>;  // exposition only
```

**2** Let `e` be an expression such that `decltype((e))` is `T`. `T` models `boolean-testable-impl` only if:

- either `remove_cvref_t<T>` is not a class type, or name lookup for the names `operator&&` and `operator||` within the scope of `remove_cvref_t<T>` as if by class member access lookup ([[class.member.lookup]](../class.member.lookup.md)) results in an empty declaration set; and
- name lookup for the names `operator&&` and `operator||` in the associated namespaces and entities of `T` ([[basic.lookup.argdep]](../basic.lookup.argdep.md)) finds no disqualifying declaration (defined below).

**3** A *disqualifying parameter* is a function parameter whose declared type `P`

- is not dependent on a template parameter, and there exists an implicit conversion sequence ([[over.best.ics]](../over.best.ics.md)) from `e` to `P`; or

- is dependent on one or more template parameters, and either

  - `P` contains no template parameter that participates in template argument deduction ([[temp.deduct.type]](../temp.deduct.type.md)), or
  - template argument deduction using the rules for deducing template arguments in a function call ([[temp.deduct.call]](../temp.deduct.call.md)) and `e` as the argument succeeds.

**4** A *key parameter* of a function template `D` is a function parameter of type *cv* `X` or reference thereto, where `X` names a specialization of a class template that is a member of the same namespace as `D`, and `X` contains at least one template parameter that participates in template argument deduction. [*Example 1*: In

```cpp
namespace Z {
  template<class> struct C {};
  template<class T>
    void operator&&(C<T> x, T y);
  template<class T>
    void operator||(C<type_identity_t<T>> x, T y);
}
```

the declaration of `Z::operator&&` contains one key parameter, `C<T> x`, and the declaration of `Z::operator||` contains no key parameters. — *end example*]

**5** A *disqualifying declaration* is

- a (non-template) function declaration that contains at least one disqualifying parameter; or

- a function template declaration that contains at least one disqualifying parameter, where

  - at least one disqualifying parameter is a key parameter; or
  - the declaration contains no key parameters; or
  - the declaration declares a function template that is not visible in its namespace ([[namespace.memdef]](../namespace.memdef.md)).

**6** [*Note 1*: The intention is to ensure that given two types `T1` and `T2` that each model `boolean-testable-impl`, the `&&` and `||` operators within the expressions `declval<T1>() && declval<T2>()` and `declval<T1>() || declval<T2>()` resolve to the corresponding built-in operators. — *end note*]

```cpp
template<class T>
  concept boolean-testable =                // exposition only
    boolean-testable-impl<T> && requires (T&& t) {
      { !std::forward<T>(t) } -> boolean-testable-impl;
    };
```

**7** Let `e` be an expression such that `decltype((e))` is `T`. `T` models `boolean-testable` only if `bool(e) == !bool(!e)`.

**8** [*Example 2*: The types `bool`, `true_type` ([[meta.type.synop]](meta.type.synop.md)), `int*`, and `bitset<N>::reference` ([[template.bitset]](template.bitset.md)) model `boolean-testable`. — *end example*]
