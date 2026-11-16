# 23.3.4.7 Concept `sentinel_for` [iterator.concept.sentinel]

[Contents](../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4.6 Concept `input_or_output_iterator` \[iterator.concept.iterator\]](iterator.concept.iterator.md) · next: [23.3.4.8 Concept `sized_sentinel_for` \[iterator.concept.sizedsentinel\]](iterator.concept.sizedsentinel.md)

**1** The `sentinel_for` concept specifies the relationship between an `input_or_output_iterator` type and a `semiregular` type whose values denote a range.

```cpp
template<class S, class I>
  concept sentinel_for =
    semiregular<S> &&
    input_or_output_iterator<I> &&
    weakly-equality-comparable-with<S, I>; // See [concept.equalitycomparable]
```

**2** Let `s` and `i` be values of type `S` and `I` such that `[i, s)` denotes a range. Types `S` and `I` model `sentinel_for<S, I>` only if

- `i == s` is well-defined.
- If `bool(i != s)` then `i` is dereferenceable and `[++i, s)` denotes a range.

**3** The domain of `==` is not static. Given an iterator `i` and sentinel `s` such that `[i, s)` denotes a range and `i != s`, `i` and `s` are not required to continue to denote a range after incrementing any other iterator equal to `i`. Consequently, `i == s` is no longer required to be well-defined.
