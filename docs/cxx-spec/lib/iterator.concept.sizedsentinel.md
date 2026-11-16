# 23.3.4.8 Concept `sized_sentinel_for` [iterator.concept.sizedsentinel]

[Contents](../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4.7 Concept `sentinel_for` \[iterator.concept.sentinel\]](iterator.concept.sentinel.md) · next: [23.3.4.9 Concept `input_iterator` \[iterator.concept.input\]](iterator.concept.input.md)

**1** The `sized_sentinel_for` concept specifies requirements on an `input_or_output_iterator` type `I` and a corresponding `sentinel_for<I>` that allow the use of the `-` operator to compute the distance between them in constant time.

```cpp
template<class S, class I>
  concept sized_sentinel_for =
    sentinel_for<S, I> &&
    !disable_sized_sentinel_for<remove_cv_t<S>, remove_cv_t<I>> &&
    requires(const I& i, const S& s) {
      { s - i } -> same_as<iter_difference_t<I>>;
      { i - s } -> same_as<iter_difference_t<I>>;
    };
```

**2** Let `i` be an iterator of type `I`, and `s` a sentinel of type `S` such that `[i, s)` denotes a range. Let *N* be the smallest number of applications of `++i` necessary to make `bool(i == s)` be `true`. `S` and `I` model `sized_sentinel_for<S, I>` only if

- If *N* is representable by `iter_difference_t<I>`, then `s - i` is well-defined and equals *N*.
- If -*N* is representable by `iter_difference_t<I>`, then `i - s` is well-defined and equals -*N*.

```cpp
template<class S, class I>
  inline constexpr bool disable_sized_sentinel_for = false;
```

**3** *Remarks:* Pursuant to [[namespace.std]](namespace.std.md), users may specialize `disable_sized_sentinel_for` for cv-unqualified non-array object types `S` and `I` if `S` and/or `I` is a program-defined type. Such specializations shall be usable in constant expressions ([[expr.const]](../expr.const.md)) and have type `const bool`.

**4** [*Note 1*: `disable_sized_sentinel_for` allows use of sentinels and iterators with the library that satisfy but do not in fact model `sized_sentinel_for`. — *end note*]

**5** [*Example 1*: The `sized_sentinel_for` concept is modeled by pairs of `random_access_iterator`s ([[iterator.concept.random.access]](iterator.concept.random.access.md)) and by counted iterators and their sentinels ([[counted.iterator]](counted.iterator.md)). — *end example*]
