# 24.7.5.2 Class template `transform_view` [range.transform.view]

[Contents](../README.md) · up: [24.7.5 Transform view \[range.transform\]](range.transform.md) · prev: [24.7.5.1 Overview \[range.transform.overview\]](range.transform.overview.md) · next: [24.7.5.3 Class template `transform_view::iterator` \[range.transform.iterator\]](range.transform.iterator.md)

```cpp
namespace std::ranges {
  template<input_range V, copy_constructible F>
    requires view<V> && is_object_v<F> &&
             regular_invocable<F&, range_reference_t<V>> &&
             can-reference<invoke_result_t<F&, range_reference_t<V>>>
  class transform_view : public view_interface<transform_view<V, F>> {
  private:
    // [range.transform.iterator], class template transform_view::iterator
    template<bool> struct iterator;             // exposition only
    // [range.transform.sentinel], class template transform_view::sentinel
    template<bool> struct sentinel;             // exposition only

    V base_ = V();                              // exposition only
    semiregular-box<F> fun_;                    // exposition only

  public:
    transform_view() = default;
    constexpr transform_view(V base, F fun);

    constexpr V base() const& requires copy_constructible<V> { return base_; }
    constexpr V base() && { return std::move(base_); }

    constexpr iterator<false> begin();
    constexpr iterator<true> begin() const
      requires range<const V> &&
               regular_invocable<const F&, range_reference_t<const V>>;

    constexpr sentinel<false> end();
    constexpr iterator<false> end() requires common_range<V>;
    constexpr sentinel<true> end() const
      requires range<const V> &&
               regular_invocable<const F&, range_reference_t<const V>>;
    constexpr iterator<true> end() const
      requires common_range<const V> &&
               regular_invocable<const F&, range_reference_t<const V>>;

    constexpr auto size() requires sized_range<V> { return ranges::size(base_); }
    constexpr auto size() const requires sized_range<const V>
    { return ranges::size(base_); }
  };

  template<class R, class F>
    transform_view(R&&, F) -> transform_view<views::all_t<R>, F>;
}
```

```cpp
constexpr transform_view(V base, F fun);
```

**1** *Effects:* Initializes `base_` with `std::move(base)` and `fun_` with `std::move(fun)`.

```cpp
constexpr iterator<false> begin();
```

**2** *Effects:* Equivalent to:

```cpp
return iterator<false>{*this, ranges::begin(base_)};
```

```cpp
constexpr iterator<true> begin() const
  requires range<const V> &&
           regular_invocable<const F&, range_reference_t<const V>>;
```

**3** *Effects:* Equivalent to:

```cpp
return iterator<true>{*this, ranges::begin(base_)};
```

```cpp
constexpr sentinel<false> end();
```

**4** *Effects:* Equivalent to:

```cpp
return sentinel<false>{ranges::end(base_)};
```

```cpp
constexpr iterator<false> end() requires common_range<V>;
```

**5** *Effects:* Equivalent to:

```cpp
return iterator<false>{*this, ranges::end(base_)};
```

```cpp
constexpr sentinel<true> end() const
  requires range<const V> &&
           regular_invocable<const F&, range_reference_t<const V>>;
```

**6** *Effects:* Equivalent to:

```cpp
return sentinel<true>{ranges::end(base_)};
```

```cpp
constexpr iterator<true> end() const
  requires common_range<const V> &&
           regular_invocable<const F&, range_reference_t<const V>>;
```

**7** *Effects:* Equivalent to:

```cpp
return iterator<true>{*this, ranges::end(base_)};
```
