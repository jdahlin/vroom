# 23.3.4.13 Concept `random_access_iterator` [iterator.concept.random.access]

[Contents](../../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4.12 Concept `bidirectional_iterator` \[iterator.concept.bidir\]](iterator.concept.bidir.md) · next: [23.3.4.14 Concept `contiguous_iterator` \[iterator.concept.contiguous\]](iterator.concept.contiguous.md)

**1** The `random_access_iterator` concept adds support for constant-time advancement with `+=`, `+`, `-=`, and `-`, as well as the computation of distance in constant time with `-`. Random access iterators also support array notation via subscripting.

```cpp
template<class I>
  concept random_access_iterator =
    bidirectional_iterator<I> &&
    derived_from<ITER_CONCEPT(I), random_access_iterator_tag> &&
    totally_ordered<I> &&
    sized_sentinel_for<I, I> &&
    requires(I i, const I j, const iter_difference_t<I> n) {
      { i += n } -> same_as<I&>;
      { j +  n } -> same_as<I>;
      { n +  j } -> same_as<I>;
      { i -= n } -> same_as<I&>;
      { j -  n } -> same_as<I>;
      {  j[n]  } -> same_as<iter_reference_t<I>>;
    };
```

**2** Let `a` and `b` be valid iterators of type `I` such that `b` is reachable from `a` after `n` applications of `++a`, let `D` be `iter_difference_t<I>`, and let `n` denote a value of type `D`. `I` models `random_access_iterator` only if

- `(a += n)` is equal to `b`.
- `addressof(a += n)` is equal to `addressof(a)`.
- `(a + n)` is equal to `(a += n)`.
- For any two positive values `x` and `y` of type `D`, if `(a + D(x + y))` is valid, then `(a + D(x + y))` is equal to `((a + x) + y)`.
- `(a + D(0))` is equal to `a`.
- If `(a + D(n - 1))` is valid, then `(a + n)` is equal to `[](I c){ return ++c; }(a + D(n - 1))`.
- `(b += D(-n))` is equal to `a`.
- `(b -= n)` is equal to `a`.
- `addressof(b -= n)` is equal to `addressof(b)`.
- `(b - n)` is equal to `(b -= n)`.
- If `b` is dereferenceable, then `a[n]` is valid and is equal to `*b`.
- `bool(a <= b)` is `true`.
