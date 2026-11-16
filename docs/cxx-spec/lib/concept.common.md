# 18.4.6 Concept `common_with` [concept.common]

[Contents](../README.md) · up: [18.4 Language-related concepts \[concepts.lang\]](concepts.lang.md) · prev: [18.4.5 Concept `common_reference_with` \[concept.commonref\]](concept.commonref.md) · next: [18.4.7 Arithmetic concepts \[concepts.arithmetic\]](concepts.arithmetic.md)

**1** If `T` and `U` can both be explicitly converted to some third type, `C`, then `T` and `U` share a *common type*, `C`. [*Note 1*: `C` could be the same as `T`, or `U`, or it could be a different type. `C` might not be unique. — *end note*]

```cpp
template<class T, class U>
  concept common_with =
    same_as<common_type_t<T, U>, common_type_t<U, T>> &&
    requires {
      static_cast<common_type_t<T, U>>(declval<T>());
      static_cast<common_type_t<T, U>>(declval<U>());
    } &&
    common_reference_with<
      add_lvalue_reference_t<const T>,
      add_lvalue_reference_t<const U>> &&
    common_reference_with<
      add_lvalue_reference_t<common_type_t<T, U>>,
      common_reference_t<
        add_lvalue_reference_t<const T>,
        add_lvalue_reference_t<const U>>>;
```

**2** Let `C` be `common_type_t<T, U>`. Let `t1` and `t2` be equality-preserving expressions ([[concepts.equality]](concepts.equality.md)) such that `decltype((t1))` and `decltype((t2))` are each `T`, and let `u1` and `u2` be equality-preserving expressions such that `decltype((u1))` and `decltype((u2))` are each `U`. `T` and `U` model `common_with<T, U>` only if:

- `C(t1)` equals `C(t2)` if and only if `t1` equals `t2`, and
- `C(u1)` equals `C(u2)` if and only if `u1` equals `u2`.

**3** [*Note 2*: Users can customize the behavior of `common_with` by specializing the `common_type` class template ([[meta.trans.other]](meta.trans.other.md)). — *end note*]
