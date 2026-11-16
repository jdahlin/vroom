# 23.3.4.12 Concept `bidirectional_iterator` [iterator.concept.bidir]

[Contents](../../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4.11 Concept `forward_iterator` \[iterator.concept.forward\]](iterator.concept.forward.md) · next: [23.3.4.13 Concept `random_access_iterator` \[iterator.concept.random.access\]](iterator.concept.random.access.md)

**1** The `bidirectional_iterator` concept adds the ability to move an iterator backward as well as forward.

```cpp
template<class I>
  concept bidirectional_iterator =
    forward_iterator<I> &&
    derived_from<ITER_CONCEPT(I), bidirectional_iterator_tag> &&
    requires(I i) {
      { --i } -> same_as<I&>;
      { i-- } -> same_as<I>;
    };
```

**2** A bidirectional iterator `r` is decrementable if and only if there exists some `q` such that `++q == r`. Decrementable iterators `r` shall be in the domain of the expressions `--r` and `r--`.

**3** Let `a` and `b` be equal objects of type `I`. `I` models `bidirectional_iterator` only if:

- If `a` and `b` are decrementable, then all of the following are `true`:

  - `addressof(--a) == addressof(a)`
  - `bool(a-- == b)`
  - after evaluating both `a--` and `--b`, `bool(a == b)` is still `true`
  - `bool(++(--a) == b)`

- If `a` and `b` are incrementable, then `bool(--(++a) == b)`.
