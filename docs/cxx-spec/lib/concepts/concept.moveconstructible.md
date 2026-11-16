# 18.4.13 Concept `move_constructible` [concept.moveconstructible]

[Contents](../../README.md) · up: [18.4 Language-related concepts \[concepts.lang\]](concepts.lang.md) · prev: [18.4.12 Concept `default_initializable` \[concept.default.init\]](concept.default.init.md) · next: [18.4.14 Concept `copy_constructible` \[concept.copyconstructible\]](concept.copyconstructible.md)

```cpp
template<class T>
  concept move_constructible = constructible_from<T, T> && convertible_to<T, T>;
```

**1** If `T` is an object type, then let `rv` be an rvalue of type `T` and `u2` a distinct object of type `T` equal to `rv`. `T` models `move_constructible` only if

- After the definition `T u = rv;`, `u` is equal to `u2`.
- `T(rv)` is equal to `u2`.
- If `T` is not `const`, `rv`’s resulting state is valid but unspecified ([[lib.types.movedfrom]](../library/lib.types.movedfrom.md)); otherwise, it is unchanged.
