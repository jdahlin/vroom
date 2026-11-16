# 24.7.5.4 Class template `transform_view::sentinel` [range.transform.sentinel]

[Contents](../README.md) · up: [24.7.5 Transform view \[range.transform\]](range.transform.md) · prev: [24.7.5.3 Class template `transform_view::iterator` \[range.transform.iterator\]](range.transform.iterator.md) · next: [24.7.6 Take view \[range.take\]](range.take.md)

```cpp
namespace std::ranges {
  template<input_range V, copy_constructible F>
    requires view<V> && is_object_v<F> &&
             regular_invocable<F&, range_reference_t<V>> &&
             can-reference<invoke_result_t<F&, range_reference_t<V>>>
  template<bool Const>
  class transform_view<V, F>::sentinel {
  private:
    using Parent =                                      // exposition only
      conditional_t<Const, const transform_view, transform_view>;
    using Base = conditional_t<Const, const V, V>;      // exposition only
    sentinel_t<Base> end_ = sentinel_t<Base>();         // exposition only
  public:
    sentinel() = default;
    constexpr explicit sentinel(sentinel_t<Base> end);
    constexpr sentinel(sentinel<!Const> i)
      requires Const && convertible_to<sentinel_t<V>, sentinel_t<Base>>;

    constexpr sentinel_t<Base> base() const;

    friend constexpr bool operator==(const iterator<Const>& x, const sentinel& y);

    friend constexpr range_difference_t<Base>
      operator-(const iterator<Const>& x, const sentinel& y)
        requires sized_sentinel_for<sentinel_t<Base>, iterator_t<Base>>;
    friend constexpr range_difference_t<Base>
      operator-(const sentinel& y, const iterator<Const>& x)
        requires sized_sentinel_for<sentinel_t<Base>, iterator_t<Base>>;
  };
}
```

```cpp
constexpr explicit sentinel(sentinel_t<Base> end);
```

**1** *Effects:* Initializes `end_` with `end`.

```cpp
constexpr sentinel(sentinel<!Const> i)
  requires Const && convertible_to<sentinel_t<V>, sentinel_t<Base>>;
```

**2** *Effects:* Initializes `end_` with `std::move(i.end_)`.

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
  operator-(const sentinel& y, const iterator<Const>& x)
    requires sized_sentinel_for<sentinel_t<Base>, iterator_t<Base>>;
```

**6** *Effects:* Equivalent to: `return y.end_ - x.current_;`
