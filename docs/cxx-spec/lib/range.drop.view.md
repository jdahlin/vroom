# 24.7.8.2 Class template `drop_view` [range.drop.view]

[Contents](../README.md) · up: [24.7.8 Drop view \[range.drop\]](range.drop.md) · prev: [24.7.8.1 Overview \[range.drop.overview\]](range.drop.overview.md) · next: [24.7.9 Drop while view \[range.drop.while\]](range.drop.while.md)

```cpp
namespace std::ranges {
  template<view V>
  class drop_view : public view_interface<drop_view<V>> {
  public:
    drop_view() = default;
    constexpr drop_view(V base, range_difference_t<V> count);

    constexpr V base() const& requires copy_constructible<V> { return base_; }
    constexpr V base() && { return std::move(base_); }

    constexpr auto begin()
      requires (!(simple-view<V> && random_access_range<V>));
    constexpr auto begin() const
      requires random_access_range<const V>;

    constexpr auto end()
      requires (!simple-view<V>)
    { return ranges::end(base_); }

    constexpr auto end() const
      requires range<const V>
    { return ranges::end(base_); }

    constexpr auto size()
      requires sized_range<V>
    {
      const auto s = ranges::size(base_);
      const auto c = static_cast<decltype(s)>(count_);
      return s < c ? 0 : s - c;
    }

    constexpr auto size() const
      requires sized_range<const V>
    {
      const auto s = ranges::size(base_);
      const auto c = static_cast<decltype(s)>(count_);
      return s < c ? 0 : s - c;
    }
  private:
    V base_ = V();                              // exposition only
    range_difference_t<V> count_ = 0;           // exposition only
  };

  template<class R>
    drop_view(R&&, range_difference_t<R>) -> drop_view<views::all_t<R>>;
}
```

```cpp
constexpr drop_view(V base, range_difference_t<V> count);
```

**1** *Preconditions:* `count >= 0` is `true`.

**2** *Effects:* Initializes `base_` with `std::move(base)` and `count_` with `count`.

```cpp
constexpr auto begin()
  requires (!(simple-view<V> && random_access_range<V>));
constexpr auto begin() const
  requires random_access_range<const V>;
```

**3** *Returns:* `ranges::next(ranges::begin(base_), count_, ranges::end(base_))`.

**4** *Remarks:* In order to provide the amortized constant-time complexity required by the `range` concept when `drop_view` models `forward_range`, the first overload caches the result within the `drop_view` for use on subsequent calls. [*Note 1*: Without this, applying a `reverse_view` over a `drop_view` would have quadratic iteration complexity. — *end note*]
