# 18.4.14 Concept `copy_constructible` [concept.copyconstructible]

[Contents](../README.md) · up: [18.4 Language-related concepts \[concepts.lang\]](concepts.lang.md) · prev: [18.4.13 Concept `move_constructible` \[concept.moveconstructible\]](concept.moveconstructible.md) · next: [18.5 Comparison concepts \[concepts.compare\]](concepts.compare.md)

```cpp
template<class T>
  concept copy_constructible =
    move_constructible<T> &&
    constructible_from<T, T&> && convertible_to<T&, T> &&
    constructible_from<T, const T&> && convertible_to<const T&, T> &&
    constructible_from<T, const T> && convertible_to<const T, T>;
```

**1** If `T` is an object type, then let `v` be an lvalue of type (possibly `const`) `T` or an rvalue of type `const T`. `T` models `copy_constructible` only if

- After the definition `T u = v;`, `u` is equal to `v` ([[concepts.equality]](concepts.equality.md)) and `v` is not modified.
- `T(v)` is equal to `v` and does not modify `v`.
