# 24.7.14.2 Class template `reverse_view` [range.reverse.view]

[Contents](../README.md) · up: [24.7.14 Reverse view \[range.reverse\]](range.reverse.md) · prev: [24.7.14.1 Overview \[range.reverse.overview\]](range.reverse.overview.md) · next: [24.7.15 Elements view \[range.elements\]](range.elements.md)

```cpp
namespace std::ranges {
  template<view V>
    requires bidirectional_range<V>
  class reverse_view : public view_interface<reverse_view<V>> {
  private:
    V base_ = V();  // exposition only
  public:
    reverse_view() = default;

    constexpr explicit reverse_view(V r);

    constexpr V base() const& requires copy_constructible<V> { return base_; }
    constexpr V base() && { return std::move(base_); }

    constexpr reverse_iterator<iterator_t<V>> begin();
    constexpr reverse_iterator<iterator_t<V>> begin() requires common_range<V>;
    constexpr auto begin() const requires common_range<const V>;

    constexpr reverse_iterator<iterator_t<V>> end();
    constexpr auto end() const requires common_range<const V>;

    constexpr auto size() requires sized_range<V> {
      return ranges::size(base_);
    }
    constexpr auto size() const requires sized_range<const V> {
      return ranges::size(base_);
    }
  };

  template<class R>
    reverse_view(R&&) -> reverse_view<views::all_t<R>>;
}
```

```cpp
constexpr explicit reverse_view(V base);
```

**1** *Effects:* Initializes `base_` with `std::move(base)`.

```cpp
constexpr reverse_iterator<iterator_t<V>> begin();
```

**2** *Returns:*

```cpp
make_reverse_iterator(ranges::next(ranges::begin(base_), ranges::end(base_)))
```

**3** *Remarks:* In order to provide the amortized constant time complexity required by the `range` concept, this function caches the result within the `reverse_view` for use on subsequent calls.

```cpp
constexpr reverse_iterator<iterator_t<V>> begin() requires common_range<V>;
constexpr auto begin() const requires common_range<const V>;
```

**4** *Effects:* Equivalent to: `return make_reverse_iterator(ranges::end(base_));`

```cpp
constexpr reverse_iterator<iterator_t<V>> end();
constexpr auto end() const requires common_range<const V>;
```

**5** *Effects:* Equivalent to: `return make_reverse_iterator(ranges::begin(base_));`
