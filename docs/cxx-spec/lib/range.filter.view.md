# 24.7.4.2 Class template `filter_view` [range.filter.view]

[Contents](../README.md) · up: [24.7.4 Filter view \[range.filter\]](range.filter.md) · prev: [24.7.4.1 Overview \[range.filter.overview\]](range.filter.overview.md) · next: [24.7.4.3 Class `filter_view::iterator` \[range.filter.iterator\]](range.filter.iterator.md)

```cpp
namespace std::ranges {
  template<input_range V, indirect_unary_predicate<iterator_t<V>> Pred>
    requires view<V> && is_object_v<Pred>
  class filter_view : public view_interface<filter_view<V, Pred>> {
  private:
    V base_ = V();                      // exposition only
    semiregular-box<Pred> pred_;  // exposition only

    // [range.filter.iterator], class filter_view::iterator
    class iterator;                     // exposition only
    // [range.filter.sentinel], class filter_view::sentinel
    class sentinel;                     // exposition only

  public:
    filter_view() = default;
    constexpr filter_view(V base, Pred pred);

    constexpr V base() const& requires copy_constructible<V> { return base_; }
    constexpr V base() && { return std::move(base_); }

    constexpr const Pred& pred() const;

    constexpr iterator begin();
    constexpr auto end() {
      if constexpr (common_range<V>)
        return iterator{*this, ranges::end(base_)};
      else
        return sentinel{*this};
    }
  };

  template<class R, class Pred>
    filter_view(R&&, Pred) -> filter_view<views::all_t<R>, Pred>;
}
```

```cpp
constexpr filter_view(V base, Pred pred);
```

**1** *Effects:* Initializes `base_` with `std::move(base)` and initializes `pred_` with `std::move(pred)`.

```cpp
constexpr const Pred& pred() const;
```

**2** *Effects:* Equivalent to: `return *pred_;`

```cpp
constexpr iterator begin();
```

**3** *Preconditions:* `pred_.has_value()`.

**4** *Returns:* `{*this, ranges::find_if(base_, ref(*pred_))}`.

**5** *Remarks:* In order to provide the amortized constant time complexity required by the `range` concept when `filter_view` models `forward_range`, this function caches the result within the `filter_view` for use on subsequent calls.
