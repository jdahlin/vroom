# 24.7.6.2 Class template `take_view` [range.take.view]

[Contents](../../README.md) · up: [24.7.6 Take view \[range.take\]](range.take.md) · prev: [24.7.6.1 Overview \[range.take.overview\]](range.take.overview.md) · next: [24.7.6.3 Class template `take_view::sentinel` \[range.take.sentinel\]](range.take.sentinel.md)

```cpp
namespace std::ranges {
  template<view V>
  class take_view : public view_interface<take_view<V>> {
  private:
    V base_ = V();                                      // exposition only
    range_difference_t<V> count_ = 0;                   // exposition only
    // [range.take.sentinel], class template take_view::sentinel
    template<bool> struct sentinel;                     // exposition only
  public:
    take_view() = default;
    constexpr take_view(V base, range_difference_t<V> count);

    constexpr V base() const& requires copy_constructible<V> { return base_; }
    constexpr V base() && { return std::move(base_); }

    constexpr auto begin() requires (!simple-view<V>) {
      if constexpr (sized_range<V>) {
        if constexpr (random_access_range<V>)
          return ranges::begin(base_);
        else {
          auto sz = size();
          return counted_iterator{ranges::begin(base_), sz};
        }
      } else
        return counted_iterator{ranges::begin(base_), count_};
    }

    constexpr auto begin() const requires range<const V> {
      if constexpr (sized_range<const V>) {
        if constexpr (random_access_range<const V>)
          return ranges::begin(base_);
        else {
          auto sz = size();
          return counted_iterator{ranges::begin(base_), sz};
        }
      } else
        return counted_iterator{ranges::begin(base_), count_};
    }

    constexpr auto end() requires (!simple-view<V>) {
      if constexpr (sized_range<V>) {
        if constexpr (random_access_range<V>)
          return ranges::begin(base_) + size();
        else
          return default_sentinel;
      } else
        return sentinel<false>{ranges::end(base_)};
    }

    constexpr auto end() const requires range<const V> {
      if constexpr (sized_range<const V>) {
        if constexpr (random_access_range<const V>)
          return ranges::begin(base_) + size();
        else
          return default_sentinel;
      } else
        return sentinel<true>{ranges::end(base_)};
    }

    constexpr auto size() requires sized_range<V> {
      auto n = ranges::size(base_);
      return ranges::min(n, static_cast<decltype(n)>(count_));
    }

    constexpr auto size() const requires sized_range<const V> {
      auto n = ranges::size(base_);
      return ranges::min(n, static_cast<decltype(n)>(count_));
    }
  };

  template<range R>
    take_view(R&&, range_difference_t<R>)
      -> take_view<views::all_t<R>>;
}
```

```cpp
constexpr take_view(V base, range_difference_t<V> count);
```

**1** *Effects:* Initializes `base_` with `std::move(base)` and `count_` with `count`.
