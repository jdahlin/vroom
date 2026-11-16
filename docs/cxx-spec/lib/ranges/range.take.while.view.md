# 24.7.7.2 Class template `take_while_view` [range.take.while.view]

[Contents](../../README.md) · up: [24.7.7 Take while view \[range.take.while\]](range.take.while.md) · prev: [24.7.7.1 Overview \[range.take.while.overview\]](range.take.while.overview.md) · next: [24.7.7.3 Class template `take_while_view::sentinel` \[range.take.while.sentinel\]](range.take.while.sentinel.md)

```cpp
namespace std::ranges {
  template<view V, class Pred>
    requires input_range<V> && is_object_v<Pred> &&
             indirect_unary_predicate<const Pred, iterator_t<V>>
  class take_while_view : public view_interface<take_while_view<V, Pred>> {
    // [range.take.while.sentinel], class template take_while_view::sentinel
    template<bool> class sentinel;                      // exposition only

    V base_ = V();                                      // exposition only
    semiregular-box<Pred> pred_;                        // exposition only

  public:
    take_while_view() = default;
    constexpr take_while_view(V base, Pred pred);

    constexpr V base() const& requires copy_constructible<V> { return base_; }
    constexpr V base() && { return std::move(base_); }

    constexpr const Pred& pred() const;

    constexpr auto begin() requires (!simple-view<V>)
    { return ranges::begin(base_); }

    constexpr auto begin() const requires range<const V>
    { return ranges::begin(base_); }

    constexpr auto end() requires (!simple-view<V>)
    { return sentinel<false>(ranges::end(base_), addressof(*pred_)); }

    constexpr auto end() const requires range<const V>
    { return sentinel<true>(ranges::end(base_), addressof(*pred_)); }
  };

  template<class R, class Pred>
    take_while_view(R&&, Pred) -> take_while_view<views::all_t<R>, Pred>;
}
```

```cpp
constexpr take_while_view(V base, Pred pred);
```

**1** *Effects:* Initializes `base_` with `std::move(base)` and `pred_` with `std::move(pred)`.

```cpp
constexpr const Pred& pred() const;
```

**2** *Effects:* Equivalent to: `return *pred_;`
