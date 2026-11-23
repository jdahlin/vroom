# 24.7.15.2 Class template `elements_view` [range.elements.view]

[Contents](../../README.md) · up: [24.7.15 Elements view \[range.elements\]](range.elements.md) · prev: [24.7.15.1 Overview \[range.elements.overview\]](range.elements.overview.md) · next: [24.7.15.3 Class template `elements_view::iterator` \[range.elements.iterator\]](range.elements.iterator.md)

```cpp
namespace std::ranges {
  template<class T, size_t N>
  concept has-tuple-element =                   // exposition only
    requires(T t) {
      typename tuple_size<T>::type;
      requires N < tuple_size_v<T>;
      typename tuple_element_t<N, T>;
      { get<N>(t) } -> convertible_to<const tuple_element_t<N, T>&>;
    };


  template<input_range V, size_t N>
    requires view<V> && has-tuple-element<range_value_t<V>, N> &&
             has-tuple-element<remove_reference_t<range_reference_t<V>>, N>
  class elements_view : public view_interface<elements_view<V, N>> {
  public:
    elements_view() = default;
    constexpr explicit elements_view(V base);

    constexpr V base() const& requires copy_constructible<V> { return base_; }
    constexpr V base() && { return std::move(base_); }

    constexpr auto begin() requires (!simple-view<V>)
    { return iterator<false>(ranges::begin(base_)); }

    constexpr auto begin() const requires simple-view<V>
    { return iterator<true>(ranges::begin(base_)); }

    constexpr auto end()
    { return sentinel<false>{ranges::end(base_)}; }

    constexpr auto end() requires common_range<V>
    { return iterator<false>{ranges::end(base_)}; }

    constexpr auto end() const requires range<const V>
    { return sentinel<true>{ranges::end(base_)}; }

    constexpr auto end() const requires common_range<const V>
    { return iterator<true>{ranges::end(base_)}; }

    constexpr auto size() requires sized_range<V>
    { return ranges::size(base_); }

    constexpr auto size() const requires sized_range<const V>
    { return ranges::size(base_); }

  private:
    // [range.elements.iterator], class template elements_view::iterator
    template<bool> struct iterator;                     // exposition only
    // [range.elements.sentinel], class template elements_view::sentinel
    template<bool> struct sentinel;                     // exposition only
    V base_ = V();                                      // exposition only
  };
}
```

```cpp
constexpr explicit elements_view(V base);
```

**1** *Effects:* Initializes `base_` with `std::move(base)`.
