# 24.7.11.2 Class template `split_view` [range.split.view]

[Contents](../../README.md) · up: [24.7.11 Split view \[range.split\]](range.split.md) · prev: [24.7.11.1 Overview \[range.split.overview\]](range.split.overview.md) · next: [24.7.11.3 Class template `split_view::outer-iterator` \[range.split.outer\]](range.split.outer.md)

```cpp
namespace std::ranges {
  template<auto> struct require-constant;       // exposition only

  template<class R>
  concept tiny-range =                          // exposition only
    sized_range<R> &&
    requires { typename require-constant<remove_reference_t<R>::size()>; } &&
    (remove_reference_t<R>::size() <= 1);

  template<input_range V, forward_range Pattern>
    requires view<V> && view<Pattern> &&
             indirectly_comparable<iterator_t<V>, iterator_t<Pattern>, ranges::equal_to> &&
             (forward_range<V> || tiny-range<Pattern>)
  class split_view : public view_interface<split_view<V, Pattern>> {
  private:
    V base_ = V();                              // exposition only
    Pattern pattern_ = Pattern();               // exposition only
    iterator_t<V> current_ = iterator_t<V>();   // exposition only, present only if !forward_range<V>
    // [range.split.outer], class template split_view::outer-iterator
    template<bool> struct outer-iterator;       // exposition only
    // [range.split.inner], class template split_view::inner-iterator
    template<bool> struct inner-iterator;       // exposition only
  public:
    split_view() = default;
    constexpr split_view(V base, Pattern pattern);

    template<input_range R>
      requires constructible_from<V, views::all_t<R>> &&
               constructible_from<Pattern, single_view<range_value_t<R>>>
    constexpr split_view(R&& r, range_value_t<R> e);

    constexpr V base() const& requires copy_constructible<V> { return base_; }
    constexpr V base() && { return std::move(base_); }

    constexpr auto begin() {
      if constexpr (forward_range<V>)
        return outer-iterator<simple-view<V>>{*this, ranges::begin(base_)};
      else {
        current_ = ranges::begin(base_);
        return outer-iterator<false>{*this};
      }
    }

    constexpr auto begin() const requires forward_range<V> && forward_range<const V> {
      return outer-iterator<true>{*this, ranges::begin(base_)};
    }

    constexpr auto end() requires forward_range<V> && common_range<V> {
      return outer-iterator<simple-view<V>>{*this, ranges::end(base_)};
    }

    constexpr auto end() const {
      if constexpr (forward_range<V> && forward_range<const V> && common_range<const V>)
        return outer-iterator<true>{*this, ranges::end(base_)};
      else
        return default_sentinel;
    }
  };

  template<class R, class P>
    split_view(R&&, P&&) -> split_view<views::all_t<R>, views::all_t<P>>;

  template<input_range R>
    split_view(R&&, range_value_t<R>)
      -> split_view<views::all_t<R>, single_view<range_value_t<R>>>;
}
```

```cpp
constexpr split_view(V base, Pattern pattern);
```

**1** *Effects:* Initializes `base_` with `std::move(base)`, and `pattern_` with `std::move(pattern)`.

```cpp
template<input_range R>
  requires constructible_from<V, views::all_t<R>> &&
           constructible_from<Pattern, single_view<range_value_t<R>>>
constexpr split_view(R&& r, range_value_t<R> e);
```

**2** *Effects:* Initializes `base_` with `views::all(std::forward<R>(r))`, and `pattern_` with `single_view{std::move(e)}`.
