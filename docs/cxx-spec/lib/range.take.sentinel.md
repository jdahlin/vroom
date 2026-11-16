# 24.7.6.3 Class template `take_view::sentinel` [range.take.sentinel]

[Contents](../README.md) · up: [24.7.6 Take view \[range.take\]](range.take.md) · prev: [24.7.6.2 Class template `take_view` \[range.take.view\]](range.take.view.md) · next: [24.7.7 Take while view \[range.take.while\]](range.take.while.md)

```cpp
namespace std::ranges {
  template<view V>
  template<bool Const>
  class take_view<V>::sentinel {
  private:
    using Base = conditional_t<Const, const V, V>;      // exposition only
    using CI = counted_iterator<iterator_t<Base>>;      // exposition only
    sentinel_t<Base> end_ = sentinel_t<Base>();         // exposition only
  public:
    sentinel() = default;
    constexpr explicit sentinel(sentinel_t<Base> end);
    constexpr sentinel(sentinel<!Const> s)
      requires Const && convertible_to<sentinel_t<V>, sentinel_t<Base>>;

    constexpr sentinel_t<Base> base() const;

    friend constexpr bool operator==(const CI& y, const sentinel& x);
  };
}
```

```cpp
constexpr explicit sentinel(sentinel_t<Base> end);
```

**1** *Effects:* Initializes `end_` with `end`.

```cpp
constexpr sentinel(sentinel<!Const> s)
  requires Const && convertible_to<sentinel_t<V>, sentinel_t<Base>>;
```

**2** *Effects:* Initializes `end_` with `std::move(s.end_)`.

```cpp
constexpr sentinel_t<Base> base() const;
```

**3** *Effects:* Equivalent to: `return end_;`

```cpp
friend constexpr bool operator==(const CI& y, const sentinel& x);
```

**4** *Effects:* Equivalent to: `return y.count() == 0 || y.base() == x.end_;`
