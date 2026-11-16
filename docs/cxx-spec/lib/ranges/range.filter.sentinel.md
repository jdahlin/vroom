# 24.7.4.4 Class `filter_view::sentinel` [range.filter.sentinel]

[Contents](../../README.md) · up: [24.7.4 Filter view \[range.filter\]](range.filter.md) · prev: [24.7.4.3 Class `filter_view::iterator` \[range.filter.iterator\]](range.filter.iterator.md) · next: [24.7.5 Transform view \[range.transform\]](range.transform.md)

```cpp
namespace std::ranges {
  template<input_range V, indirect_unary_predicate<iterator_t<V>> Pred>
    requires view<V> && is_object_v<Pred>
  class filter_view<V, Pred>::sentinel {
  private:
    sentinel_t<V> end_ = sentinel_t<V>();       // exposition only
  public:
    sentinel() = default;
    constexpr explicit sentinel(filter_view& parent);

    constexpr sentinel_t<V> base() const;

    friend constexpr bool operator==(const iterator& x, const sentinel& y);
  };
}
```

```cpp
constexpr explicit sentinel(filter_view& parent);
```

**1** *Effects:* Initializes `end_` with `ranges::end(parent.base_)`.

```cpp
constexpr sentinel_t<V> base() const;
```

**2** *Effects:* Equivalent to: `return end_;`

```cpp
friend constexpr bool operator==(const iterator& x, const sentinel& y);
```

**3** *Effects:* Equivalent to: `return x.current_ == y.end_;`
