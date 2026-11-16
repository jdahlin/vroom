# 24.7.15.4 Class template `elements_view::sentinel` [range.elements.sentinel]

[Contents](../../README.md) · up: [24.7.15 Elements view \[range.elements\]](range.elements.md) · prev: [24.7.15.3 Class template `elements_view::iterator` \[range.elements.iterator\]](range.elements.iterator.md) · next: [25 Algorithms library \[algorithms\]](../algorithms/algorithms.md)

```cpp
namespace std::ranges {
  template<input_range V, size_t N>
    requires view<V> && has-tuple-element<range_value_t<V>, N> &&
             has-tuple-element<remove_reference_t<range_reference_t<V>>, N>
  template<bool Const>
  class elements_view<V, N>::sentinel {                 // exposition only
  private:
    using Base = conditional_t<Const, const V, V>;      // exposition only
    sentinel_t<Base> end_ = sentinel_t<Base>();         // exposition only
  public:
    sentinel() = default;
    constexpr explicit sentinel(sentinel_t<Base> end);
    constexpr sentinel(sentinel<!Const> other)
      requires Const && convertible_to<sentinel_t<V>, sentinel_t<Base>>;

    constexpr sentinel_t<Base> base() const;

    friend constexpr bool operator==(const iterator<Const>& x, const sentinel& y);

    friend constexpr range_difference_t<Base>
      operator-(const iterator<Const>& x, const sentinel& y)
        requires sized_sentinel_for<sentinel_t<Base>, iterator_t<Base>>;

    friend constexpr range_difference_t<Base>
      operator-(const sentinel& x, const iterator<Const>& y)
        requires sized_sentinel_for<sentinel_t<Base>, iterator_t<Base>>;
  };
}
```

```cpp
constexpr explicit sentinel(sentinel_t<Base> end);
```

**1** *Effects:* Initializes `end_` with `end`.

```cpp
constexpr sentinel(sentinel<!Const> other)
  requires Const && convertible_to<sentinel_t<V>, sentinel_t<Base>>;
```

**2** *Effects:* Initializes `end_` with `std::move(other.end_)`.

```cpp
constexpr sentinel_t<Base> base() const;
```

**3** *Effects:* Equivalent to: `return end_;`

```cpp
friend constexpr bool operator==(const iterator<Const>& x, const sentinel& y);
```

**4** *Effects:* Equivalent to: `return x.current_ == y.end_;`

```cpp
friend constexpr range_difference_t<Base>
  operator-(const iterator<Const>& x, const sentinel& y)
    requires sized_sentinel_for<sentinel_t<Base>, iterator_t<Base>>;
```

**5** *Effects:* Equivalent to: `return x.current_ - y.end_;`

```cpp
friend constexpr range_difference_t<Base>
  operator-(const sentinel& x, const iterator<Const>& y)
    requires sized_sentinel_for<sentinel_t<Base>, iterator_t<Base>>;
```

**6** *Effects:* Equivalent to: `return x.end_ - y.current_;`
