# 18.4.5 Concept `common_reference_with` [concept.commonref]

[Contents](../../README.md) · up: [18.4 Language-related concepts \[concepts.lang\]](concepts.lang.md) · prev: [18.4.4 Concept `convertible_to` \[concept.convertible\]](concept.convertible.md) · next: [18.4.6 Concept `common_with` \[concept.common\]](concept.common.md)

**1** For two types `T` and `U`, if `common_reference_t<T, U>` is well-formed and denotes a type `C` such that both `convertible_to<T, C>` and `convertible_to<U, C>` are modeled, then `T` and `U` share a *common reference type*, `C`. [*Note 1*: `C` could be the same as `T`, or `U`, or it could be a different type. `C` may be a reference type. — *end note*]

```cpp
template<class T, class U>
  concept common_reference_with =
    same_as<common_reference_t<T, U>, common_reference_t<U, T>> &&
    convertible_to<T, common_reference_t<T, U>> &&
    convertible_to<U, common_reference_t<T, U>>;
```

**2** Let `C` be `common_reference_t<T, U>`. Let `t1` and `t2` be equality-preserving expressions ([[concepts.equality]](concepts.equality.md)) such that `decltype((t1))` and `decltype((t2))` are each `T`, and let `u1` and `u2` be equality-preserving expressions such that `decltype((u1))` and `decltype((u2))` are each `U`. `T` and `U` model `common_reference_with<T, U>` only if:

- `C(t1)` equals `C(t2)` if and only if `t1` equals `t2`, and
- `C(u1)` equals `C(u2)` if and only if `u1` equals `u2`.

**3** [*Note 2*: Users can customize the behavior of `common_reference_with` by specializing the `basic_common_reference` class template ([[meta.trans.other]](../utilities/meta.trans.other.md)). — *end note*]
