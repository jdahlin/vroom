# 23.3.4.14 Concept `contiguous_iterator` [iterator.concept.contiguous]

[Contents](../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4.13 Concept `random_access_iterator` \[iterator.concept.random.access\]](iterator.concept.random.access.md) · next: [23.3.5 C++17 iterator requirements \[iterator.cpp17\]](iterator.cpp17.md)

**1** The `contiguous_iterator` concept provides a guarantee that the denoted elements are stored contiguously in memory.

```cpp
template<class I>
  concept contiguous_iterator =
    random_access_iterator<I> &&
    derived_from<ITER_CONCEPT(I), contiguous_iterator_tag> &&
    is_lvalue_reference_v<iter_reference_t<I>> &&
    same_as<iter_value_t<I>, remove_cvref_t<iter_reference_t<I>>> &&
    requires(const I& i) {
      { to_address(i) } -> same_as<add_pointer_t<iter_reference_t<I>>>;
    };
```

**2** Let `a` and `b` be dereferenceable iterators and `c` be a non-dereferenceable iterator of type `I` such that `b` is reachable from `a` and `c` is reachable from `b`, and let `D` be `iter_difference_t<I>`. The type `I` models `contiguous_iterator` only if

- `to_address(a) == addressof(*a)`,
- `to_address(b) == to_address(a) + D(b - a)`, and
- `to_address(c) == to_address(a) + D(c - a)`.
