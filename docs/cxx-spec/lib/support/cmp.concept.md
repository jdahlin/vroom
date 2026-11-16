# 17.11.4 Concept `three_way_comparable` [cmp.concept]

[Contents](../../README.md) · up: [17.11 Comparisons \[cmp\]](cmp.md) · prev: [17.11.3 Class template `common_comparison_category` \[cmp.common\]](cmp.common.md) · next: [17.11.5 Result of three-way comparison \[cmp.result\]](cmp.result.md)

```cpp
template<class T, class Cat>
  concept compares-as =                 // exposition only
    same_as<common_comparison_category_t<T, Cat>, Cat>;

template<class T, class U>
  concept partially-ordered-with =      // exposition only
    requires(const remove_reference_t<T>& t, const remove_reference_t<U>& u) {
      { t <  u } -> boolean-testable;
      { t >  u } -> boolean-testable;
      { t <= u } -> boolean-testable;
      { t >= u } -> boolean-testable;
      { u <  t } -> boolean-testable;
      { u >  t } -> boolean-testable;
      { u <= t } -> boolean-testable;
      { u >= t } -> boolean-testable;
    };
```

**1** Let `t` and `u` be lvalues of types `const remove_reference_t<T>` and `const remove_reference_t<U>`, respectively. `T` and `U` model `partially-ordered-with<T, U>` only if:

- `t < u`, `t <= u`, `t > u`, `t >= u`, `u < t`, `u <= t`, `u > t`, and `u >= t` have the same domain.
- `bool(t < u) == bool(u > t)` is `true`,
- `bool(u < t) == bool(t > u)` is `true`,
- `bool(t <= u) == bool(u >= t)` is `true`, and
- `bool(u <= t) == bool(t >= u)` is `true`.

```cpp
template<class T, class Cat = partial_ordering>
  concept three_way_comparable =
    weakly-equality-comparable-with<T, T> &&
    partially-ordered-with<T, T> &&
    requires(const remove_reference_t<T>& a, const remove_reference_t<T>& b) {
      { a <=> b } -> compares-as<Cat>;
    };
```

**2** Let `a` and `b` be lvalues of type `const remove_reference_t<T>`. `T` and `Cat` model `three_way_comparable<T, Cat>` only if:

- `(a <=> b == 0) == bool(a == b)` is `true`,
- `(a <=> b != 0) == bool(a != b)` is `true`,
- `((a <=> b) <=> 0)` and `(0 <=> (b <=> a))` are equal,
- `(a <=> b < 0) == bool(a < b)` is `true`,
- `(a <=> b > 0) == bool(a > b)` is `true`,
- `(a <=> b <= 0) == bool(a <= b)` is `true`,
- `(a <=> b >= 0) == bool(a >= b)` is `true`, and
- if `Cat` is convertible to `strong_ordering`, `T` models `totally_ordered` ([[concept.totallyordered]](../concepts/concept.totallyordered.md)).

```cpp
template<class T, class U, class Cat = partial_ordering>
  concept three_way_comparable_with =
    three_way_comparable<T, Cat> &&
    three_way_comparable<U, Cat> &&
    common_reference_with<const remove_reference_t<T>&, const remove_reference_t<U>&> &&
    three_way_comparable<
      common_reference_t<const remove_reference_t<T>&, const remove_reference_t<U>&>, Cat> &&
    weakly-equality-comparable-with<T, U> &&
    partially-ordered-with<T, U> &&
    requires(const remove_reference_t<T>& t, const remove_reference_t<U>& u) {
      { t <=> u } -> compares-as<Cat>;
      { u <=> t } -> compares-as<Cat>;
    };
```

**3** Let `t` and `u` be lvalues of types `const remove_reference_t<T>` and `const remove_reference_t<U>`, respectively. Let `C` be `common_reference_t<const remove_reference_t<T>&, const remove_reference_t<U>&>`. `T`, `U`, and `Cat` model `three_way_comparable_with<T, U, Cat>` only if:

- `t <=> u` and `u <=> t` have the same domain,
- `((t <=> u) <=> 0)` and `(0 <=> (u <=> t))` are equal,
- `(t <=> u == 0) == bool(t == u)` is `true`,
- `(t <=> u != 0) == bool(t != u)` is `true`,
- `Cat(t <=> u) == Cat(C(t) <=> C(u))` is `true`,
- `(t <=> u < 0) == bool(t < u)` is `true`,
- `(t <=> u > 0) == bool(t > u)` is `true`,
- `(t <=> u <= 0) == bool(t <= u)` is `true`,
- `(t <=> u >= 0) == bool(t >= u)` is `true`, and
- if `Cat` is convertible to `strong_ordering`, `T` and `U` model `totally_ordered_with<T, U>` ([[concept.totallyordered]](../concepts/concept.totallyordered.md)).
