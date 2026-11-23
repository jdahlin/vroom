# 24.5.3.1 Constructors and conversions [range.subrange.ctor]

[Contents](../../README.md) · up: [24.5.3 Sub-ranges \[range.subrange\]](range.subrange.md) · prev: [24.5.3 Sub-ranges \[range.subrange\]](range.subrange.md) · next: [24.5.3.2 Accessors \[range.subrange.access\]](range.subrange.access.md)

```cpp
constexpr subrange(convertible-to-non-slicing<I> auto i, S s) requires (!StoreSize);
```

**1** *Preconditions:* `[i, s)` is a valid range.

**2** *Effects:* Initializes `begin_` with `std::move(i)` and `end_` with `s`.

```cpp
constexpr subrange(convertible-to-non-slicing<I> auto i, S s,
                   make-unsigned-like-t<iter_difference_t<I>> n)
  requires (K == subrange_kind::sized);
```

**3** *Preconditions:* `[i, s)` is a valid range, and `n == to-unsigned-like(ranges::distance(i, s))`.

**4** *Effects:* Initializes `begin_` with `std::move(i)` and `end_` with `s`. If `StoreSize` is `true`, initializes `size_` with `n`.

**5** [*Note 1*: Accepting the length of the range and storing it to later return from `size()` enables `subrange` to model `sized_range` even when it stores an iterator and sentinel that do not model `sized_sentinel_for`. — *end note*]

```cpp
template<not-same-as<subrange> R>
  requires borrowed_range<R> &&
           convertible-to-non-slicing<iterator_t<R>, I> &&
           convertible_to<sentinel_t<R>, S>
constexpr subrange(R&& r) requires (!StoreSize || sized_range<R>);
```

**6** *Effects:* Equivalent to:

- If `StoreSize` is `true`, `subrange{r, ranges::size(r)}`.
- Otherwise, `subrange{ranges::begin(r), ranges::end(r)}`.

```cpp
template<not-same-as<subrange> PairLike>
  requires pair-like-convertible-from<PairLike, const I&, const S&>
constexpr operator PairLike() const;
```

**7** *Effects:* Equivalent to: `return PairLike(begin_, end_);`
