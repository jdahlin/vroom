# 23.3.4.2 Concept `indirectly_readable` [iterator.concept.readable]

[Contents](../../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4.1 General \[iterator.concepts.general\]](iterator.concepts.general.md) · next: [23.3.4.3 Concept `indirectly_writable` \[iterator.concept.writable\]](iterator.concept.writable.md)

**1** Types that are indirectly readable by applying `operator*` model the `indirectly_readable` concept, including pointers, smart pointers, and iterators.

```cpp
template<class In>
  concept indirectly-readable-impl =
    requires(const In in) {
      typename iter_value_t<In>;
      typename iter_reference_t<In>;
      typename iter_rvalue_reference_t<In>;
      { *in } -> same_as<iter_reference_t<In>>;
      { ranges::iter_move(in) } -> same_as<iter_rvalue_reference_t<In>>;
    } &&
    common_reference_with<iter_reference_t<In>&&, iter_value_t<In>&> &&
    common_reference_with<iter_reference_t<In>&&, iter_rvalue_reference_t<In>&&> &&
    common_reference_with<iter_rvalue_reference_t<In>&&, const iter_value_t<In>&>;
```

```cpp
template<class In>
  concept indirectly_readable =
    indirectly-readable-impl<remove_cvref_t<In>>;
```

**2** Given a value `i` of type `I`, `I` models `indirectly_readable` only if the expression `*i` is equality-preserving. [*Note 1*: The expression `*i` is indirectly required to be valid via the exposition-only `dereferenceable` concept ([[iterator.synopsis]](iterator.synopsis.md)). — *end note*]
