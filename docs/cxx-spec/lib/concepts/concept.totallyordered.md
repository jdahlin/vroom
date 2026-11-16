# 18.5.4 Concept `totally_ordered` [concept.totallyordered]

[Contents](../../README.md) · up: [18.5 Comparison concepts \[concepts.compare\]](concepts.compare.md) · prev: [18.5.3 Concept `equality_comparable` \[concept.equalitycomparable\]](concept.equalitycomparable.md) · next: [18.6 Object concepts \[concepts.object\]](concepts.object.md)

```cpp
template<class T>
  concept totally_ordered =
    equality_comparable<T> && partially-ordered-with<T, T>;
```

**1** Given a type `T`, let `a`, `b`, and `c` be lvalues of type `const remove_reference_t<T>`. `T` models `totally_ordered` only if

- Exactly one of `bool(a < b)`, `bool(a > b)`, or `bool(a == b)` is `true`.
- If `bool(a < b)` and `bool(b < c)`, then `bool(a < c)`.
- `bool(a <= b) == !bool(b < a)`.
- `bool(a >= b) == !bool(a < b)`.

```cpp
template<class T, class U>
  concept totally_ordered_with =
    totally_ordered<T> && totally_ordered<U> &&
    equality_comparable_with<T, U> &&
    totally_ordered<
      common_reference_t<
        const remove_reference_t<T>&,
        const remove_reference_t<U>&>> &&
    partially-ordered-with<T, U>;
```

**2** Given types `T` and `U`, let `t` be an lvalue of type `const remove_reference_t<T>`, `u` be an lvalue of type `const remove_reference_t<U>`, and `C` be:

```cpp
common_reference_t<const remove_reference_t<T>&, const remove_reference_t<U>&>
```

`T` and `U` model `totally_ordered_with<T, U>` only if

- `bool(t < u) == bool(C(t) < C(u)).`
- `bool(t > u) == bool(C(t) > C(u)).`
- `bool(t <= u) == bool(C(t) <= C(u)).`
- `bool(t >= u) == bool(C(t) >= C(u)).`
- `bool(u < t) == bool(C(u) < C(t)).`
- `bool(u > t) == bool(C(u) > C(t)).`
- `bool(u <= t) == bool(C(u) <= C(t)).`
- `bool(u >= t) == bool(C(u) >= C(t)).`
