# 24.7.10.2 Class template `join_view` [range.join.view]

[Contents](../README.md) · up: [24.7.10 Join view \[range.join\]](range.join.md) · prev: [24.7.10.1 Overview \[range.join.overview\]](range.join.overview.md) · next: [24.7.10.3 Class template `join_view::iterator` \[range.join.iterator\]](range.join.iterator.md)

```cpp
namespace std::ranges {
  template<input_range V>
    requires view<V> && input_range<range_reference_t<V>> &&
             (is_reference_v<range_reference_t<V>> ||
              view<range_value_t<V>>)
  class join_view : public view_interface<join_view<V>> {
  private:
    using InnerRng =                    // exposition only
      range_reference_t<V>;
    // [range.join.iterator], class template join_view::iterator
    template<bool Const>
      struct iterator;                  // exposition only
    // [range.join.sentinel], class template join_view::sentinel
    template<bool Const>
      struct sentinel;                  // exposition only

    V base_ = V();                      // exposition only
    views::all_t<InnerRng> inner_ =     // exposition only, present only when !is_reference_v<InnerRng>
      views::all_t<InnerRng>();
  public:
    join_view() = default;
    constexpr explicit join_view(V base);

    constexpr V base() const& requires copy_constructible<V> { return base_; }
    constexpr V base() && { return std::move(base_); }

    constexpr auto begin() {
      constexpr bool use_const = simple-view<V> &&
                                 is_reference_v<range_reference_t<V>>;
      return iterator<use_const>{*this, ranges::begin(base_)};
    }

    constexpr auto begin() const
    requires input_range<const V> &&
             is_reference_v<range_reference_t<const V>> {
      return iterator<true>{*this, ranges::begin(base_)};
    }

    constexpr auto end() {
      if constexpr (forward_range<V> &&
                    is_reference_v<InnerRng> && forward_range<InnerRng> &&
                    common_range<V> && common_range<InnerRng>)
        return iterator<simple-view<V>>{*this, ranges::end(base_)};
      else
        return sentinel<simple-view<V>>{*this};
    }

    constexpr auto end() const
    requires input_range<const V> &&
             is_reference_v<range_reference_t<const V>> {
      if constexpr (forward_range<const V> &&
                    is_reference_v<range_reference_t<const V>> &&
                    forward_range<range_reference_t<const V>> &&
                    common_range<const V> &&
                    common_range<range_reference_t<const V>>)
        return iterator<true>{*this, ranges::end(base_)};
      else
        return sentinel<true>{*this};
    }
  };

  template<class R>
    explicit join_view(R&&) -> join_view<views::all_t<R>>;
}
```

```cpp
constexpr explicit join_view(V base);
```

**1** *Effects:* Initializes `base_` with `std::move(base)`.
