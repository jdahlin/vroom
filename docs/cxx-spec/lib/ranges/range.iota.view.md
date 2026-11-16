# 24.6.3.2 Class template `iota_view` [range.iota.view]

[Contents](../../README.md) · up: [24.6.3 Iota view \[range.iota\]](range.iota.md) · prev: [24.6.3.1 Overview \[range.iota.overview\]](range.iota.overview.md) · next: [24.6.3.3 Class `iota_view::iterator` \[range.iota.iterator\]](range.iota.iterator.md)

```cpp
namespace std::ranges {
  template<class I>
    concept decrementable =     // exposition only
      see below;
  template<class I>
    concept advanceable =       // exposition only
      see below;

  template<weakly_incrementable W, semiregular Bound = unreachable_sentinel_t>
    requires weakly-equality-comparable-with<W, Bound> && semiregular<W>
  class iota_view : public view_interface<iota_view<W, Bound>> {
  private:
    // [range.iota.iterator], class iota_view::iterator
    struct iterator;            // exposition only
    // [range.iota.sentinel], class iota_view::sentinel
    struct sentinel;            // exposition only
    W value_ = W();             // exposition only
    Bound bound_ = Bound();     // exposition only
  public:
    iota_view() = default;
    constexpr explicit iota_view(W value);
    constexpr iota_view(type_identity_t<W> value,
                        type_identity_t<Bound> bound);
    constexpr iota_view(iterator first, sentinel last) : iota_view(*first, last.bound_) {}

    constexpr iterator begin() const;
    constexpr auto end() const;
    constexpr iterator end() const requires same_as<W, Bound>;

    constexpr auto size() const requires see below;
  };

  template<class W, class Bound>
    requires (!is-integer-like<W> || !is-integer-like<Bound> ||
              (is-signed-integer-like<W> == is-signed-integer-like<Bound>))
    iota_view(W, Bound) -> iota_view<W, Bound>;
}
```

**1** Let `IOTA-DIFF-T(W)` be defined as follows:

- If `W` is not an integral type, or if it is an integral type and `sizeof(iter_difference_t<W>)` is greater than `sizeof(W)`, then `IOTA-DIFF-T(W)` denotes `iter_difference_t<W>`.
- Otherwise, `IOTA-DIFF-T(W)` is a signed integer type of width greater than the width of `W` if such a type exists.
- Otherwise, `IOTA-DIFF-T(W)` is an unspecified signed-integer-like type ([[iterator.concept.winc]](../iterators/iterator.concept.winc.md)) of width not less than the width of `W`. [*Note 1*: It is unspecified whether this type satisfies `weakly_incrementable`. — *end note*]

**2** The exposition-only `decrementable` concept is equivalent to:

```cpp
template<class I>
  concept decrementable =
    incrementable<I> && requires(I i) {
      { --i } -> same_as<I&>;
      { i-- } -> same_as<I>;
    };
```

**3** When an object is in the domain of both pre- and post-decrement, the object is said to be *decrementable*.

**4** Let `a` and `b` be equal objects of type `I`. `I` models `decrementable` only if

- If `a` and `b` are decrementable, then the following are all true:

  - `addressof(--a) == addressof(a)`
  - `bool(a-- == b)`
  - `bool(((void)a--, a) == --b)`
  - `bool(++(--a) == b)`.

- If `a` and `b` are incrementable, then `bool(--(++a) == b)`.

**5** The exposition-only `advanceable` concept is equivalent to:

```cpp
template<class I>
  concept advanceable =
    decrementable<I> && totally_ordered<I> &&
    requires(I i, const I j, const IOTA-DIFF-T(I) n) {
      { i += n } -> same_as<I&>;
      { i -= n } -> same_as<I&>;
      I(j + n);
      I(n + j);
      I(j - n);
      { j - j } -> convertible_to<IOTA-DIFF-T(I)>;
    };
```

Let `D` be `IOTA-DIFF-T(I)`. Let `a` and `b` be objects of type `I` such that `b` is reachable from `a` after `n` applications of `++a`, for some value `n` of type `D`. `I` models `advanceable` only if

- `(a += n)` is equal to `b`.
- `addressof(a += n)` is equal to `addressof(a)`.
- `I(a + n)` is equal to `(a += n)`.
- For any two positive values `x` and `y` of type `D`, if `I(a + D(x + y))` is well-defined, then `I(a + D(x + y))` is equal to `I(I(a + x) + y)`.
- `I(a + D(0))` is equal to `a`.
- If `I(a + D(n - 1))` is well-defined, then `I(a + n)` is equal to `[](I c) { return ++c; }(I(a + D(n - 1)))`.
- `(b += -n)` is equal to `a`.
- `(b -= n)` is equal to `a`.
- `addressof(b -= n)` is equal to `addressof(b)`.
- `I(b - n)` is equal to `(b -= n)`.
- `D(b - a)` is equal to `n`.
- `D(a - b)` is equal to `D(-n)`.
- `bool(a <= b)` is `true`.

```cpp
constexpr explicit iota_view(W value);
```

**6** *Preconditions:* `Bound` denotes `unreachable_sentinel_t` or `Bound()` is reachable from `value`.

**7** *Effects:* Initializes `value_` with `value`.

```cpp
constexpr iota_view(type_identity_t<W> value, type_identity_t<Bound> bound);
```

**8** *Preconditions:* `Bound` denotes `unreachable_sentinel_t` or `bound` is reachable from `value`. When `W` and `Bound` model `totally_ordered_with`, then `bool(value <= bound)` is `true`.

**9** *Effects:* Initializes `value_` with `value` and `bound_` with `bound`.

```cpp
constexpr iterator begin() const;
```

**10** *Effects:* Equivalent to: `return iterator{value_};`

```cpp
constexpr auto end() const;
```

**11** *Effects:* Equivalent to:

```cpp
if constexpr (same_as<Bound, unreachable_sentinel_t>)
  return unreachable_sentinel;
else
  return sentinel{bound_};
```

```cpp
constexpr iterator end() const requires same_as<W, Bound>;
```

**12** *Effects:* Equivalent to: `return iterator{bound_};`

```cpp
constexpr auto size() const requires see below;
```

**13** *Effects:* Equivalent to:

```cpp
if constexpr (is-integer-like<W> && is-integer-like<Bound>)
  return (value_ < 0)
    ? ((bound_ < 0)
      ? to-unsigned-like(-value_) - to-unsigned-like(-bound_)
      : to-unsigned-like(bound_) + to-unsigned-like(-value_))
    : to-unsigned-like(bound_) - to-unsigned-like(value_);
else
  return to-unsigned-like(bound_ - value_);
```

**14** *Remarks:* The expression in the *requires-clause* is equivalent to

```cpp
(same_as<W, Bound> && advanceable<W>) || (integral<W> && integral<Bound>) ||
  sized_sentinel_for<Bound, W>
```
