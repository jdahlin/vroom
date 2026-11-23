# 18.5.3 Concept `equality_comparable` [concept.equalitycomparable]

[Contents](../../README.md) · up: [18.5 Comparison concepts \[concepts.compare\]](concepts.compare.md) · prev: [18.5.2 Boolean testability \[concept.booleantestable\]](concept.booleantestable.md) · next: [18.5.4 Concept `totally_ordered` \[concept.totallyordered\]](concept.totallyordered.md)

```cpp
template<class T, class U>
  concept weakly-equality-comparable-with = // exposition only
    requires(const remove_reference_t<T>& t,
             const remove_reference_t<U>& u) {
      { t == u } -> boolean-testable;
      { t != u } -> boolean-testable;
      { u == t } -> boolean-testable;
      { u != t } -> boolean-testable;
    };
```

**1** Given types `T` and `U`, let `t` and `u` be lvalues of types `const remove_reference_t<T>` and `const remove_reference_t<U>` respectively. `T` and `U` model `weakly-equality-comparable-with<T, U>` only if

- `t == u`, `u == t`, `t != u`, and `u != t` have the same domain.
- `bool(u == t) == bool(t == u)`.
- `bool(t != u) == !bool(t == u)`.
- `bool(u != t) == bool(t != u)`.

```cpp
template<class T>
  concept equality_comparable = weakly-equality-comparable-with<T, T>;
```

**2** Let `a` and `b` be objects of type `T`. `T` models `equality_comparable` only if `bool(a == b)` is `true` when `a` is equal to `b` ([[concepts.equality]](concepts.equality.md)), and `false` otherwise.

**3** [*Note 1*: The requirement that the expression `a == b` is equality-preserving implies that `==` is transitive and symmetric. — *end note*]

```cpp
template<class T, class U>
  concept equality_comparable_with =
    equality_comparable<T> && equality_comparable<U> &&
    common_reference_with<const remove_reference_t<T>&, const remove_reference_t<U>&> &&
    equality_comparable<
      common_reference_t<
        const remove_reference_t<T>&,
        const remove_reference_t<U>&>> &&
    weakly-equality-comparable-with<T, U>;
```

**4** Given types `T` and `U`, let `t` be an lvalue of type `const remove_reference_t<T>`, `u` be an lvalue of type `const remove_reference_t<U>`, and `C` be:

```cpp
common_reference_t<const remove_reference_t<T>&, const remove_reference_t<U>&>
```

`T` and `U` model `equality_comparable_with<T, U>` only if `bool(t == u) == bool(C(t) == C(u))`.
