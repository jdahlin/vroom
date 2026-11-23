# 24.7.11.4 Class `split_view::outer-iterator::value_type` [range.split.outer.value]

[Contents](../../README.md) · up: [24.7.11 Split view \[range.split\]](range.split.md) · prev: [24.7.11.3 Class template `split_view::outer-iterator` \[range.split.outer\]](range.split.outer.md) · next: [24.7.11.5 Class template `split_view::inner-iterator` \[range.split.inner\]](range.split.inner.md)

```cpp
namespace std::ranges {
  template<input_range V, forward_range Pattern>
    requires view<V> && view<Pattern> &&
             indirectly_comparable<iterator_t<V>, iterator_t<Pattern>, ranges::equal_to> &&
             (forward_range<V> || tiny-range<Pattern>)
  template<bool Const>
  struct split_view<V, Pattern>::outer-iterator<Const>::value_type
    : view_interface<value_type> {
  private:
    outer-iterator i_ = outer-iterator();               // exposition only
  public:
    value_type() = default;
    constexpr explicit value_type(outer-iterator i);

    constexpr inner-iterator<Const> begin() const requires copyable<outer-iterator>;
    constexpr inner-iterator<Const> begin() requires (!copyable<outer-iterator>);
    constexpr default_sentinel_t end() const;
  };
}
```

```cpp
constexpr explicit value_type(outer-iterator i);
```

**1** *Effects:* Initializes `i_` with `std::move(i)`.

```cpp
constexpr inner-iterator<Const> begin() const requires copyable<outer-iterator>;
```

**2** *Effects:* Equivalent to: `return inner-iterator<Const>{i_};`

```cpp
constexpr inner-iterator<Const> begin() requires (!copyable<outer-iterator>);
```

**3** *Effects:* Equivalent to: `return inner-iterator<Const>{std::move(i_)};`

```cpp
constexpr default_sentinel_t end() const;
```

**4** *Effects:* Equivalent to: `return default_sentinel;`
