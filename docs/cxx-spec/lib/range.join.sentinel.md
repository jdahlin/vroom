# 24.7.10.4 Class template `join_view::sentinel` [range.join.sentinel]

[Contents](../README.md) · up: [24.7.10 Join view \[range.join\]](range.join.md) · prev: [24.7.10.3 Class template `join_view::iterator` \[range.join.iterator\]](range.join.iterator.md) · next: [24.7.11 Split view \[range.split\]](range.split.md)

```cpp
namespace std::ranges {
  template<input_range V>
    requires view<V> && input_range<range_reference_t<V>> &&
             (is_reference_v<range_reference_t<V>> ||
              view<range_value_t<V>>)
  template<bool Const>
  struct join_view<V>::sentinel {
  private:
    using Parent =                                      // exposition only
      conditional_t<Const, const join_view, join_view>;
    using Base   = conditional_t<Const, const V, V>;    // exposition only
    sentinel_t<Base> end_ = sentinel_t<Base>();         // exposition only
  public:
    sentinel() = default;

    constexpr explicit sentinel(Parent& parent);
    constexpr sentinel(sentinel<!Const> s)
      requires Const && convertible_to<sentinel_t<V>, sentinel_t<Base>>;

    friend constexpr bool operator==(const iterator<Const>& x, const sentinel& y);
  };
}
```

```cpp
constexpr explicit sentinel(Parent& parent);
```

**1** *Effects:* Initializes `end_` with `ranges::end(parent.base_)`.

```cpp
constexpr sentinel(sentinel<!Const> s)
  requires Const && convertible_to<sentinel_t<V>, sentinel_t<Base>>;
```

**2** *Effects:* Initializes `end_` with `std::move(s.end_)`.

```cpp
friend constexpr bool operator==(const iterator<Const>& x, const sentinel& y);
```

**3** *Effects:* Equivalent to: `return x.outer_ == y.end_;`
