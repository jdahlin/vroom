# 24.7.9.2 Class template `drop_while_view` [range.drop.while.view]

[Contents](../README.md) · up: [24.7.9 Drop while view \[range.drop.while\]](range.drop.while.md) · prev: [24.7.9.1 Overview \[range.drop.while.overview\]](range.drop.while.overview.md) · next: [24.7.10 Join view \[range.join\]](range.join.md)

```cpp
namespace std::ranges {
  template<view V, class Pred>
    requires input_range<V> && is_object_v<Pred> &&
             indirect_unary_predicate<const Pred, iterator_t<V>>
  class drop_while_view : public view_interface<drop_while_view<V, Pred>> {
  public:
    drop_while_view() = default;
    constexpr drop_while_view(V base, Pred pred);

    constexpr V base() const& requires copy_constructible<V> { return base_; }
    constexpr V base() && { return std::move(base_); }

    constexpr const Pred& pred() const;

    constexpr auto begin();

    constexpr auto end()
    { return ranges::end(base_); }

  private:
    V base_ = V();                                      // exposition only
    semiregular-box<Pred> pred_;                        // exposition only
  };

  template<class R, class Pred>
    drop_while_view(R&&, Pred) -> drop_while_view<views::all_t<R>, Pred>;
}
```

```cpp
constexpr drop_while_view(V base, Pred pred);
```

**1** *Effects:* Initializes `base_` with `std::move(base)` and `pred_` with `std::move(pred)`.

```cpp
constexpr const Pred& pred() const;
```

**2** *Effects:* Equivalent to: `return *pred_;`

```cpp
constexpr auto begin();
```

**3** *Returns:* `ranges::find_if_not(base_, cref(*pred_))`.

**4** *Remarks:* In order to provide the amortized constant-time complexity required by the `range` concept when `drop_while_view` models `forward_range`, the first call caches the result within the `drop_while_view` for use on subsequent calls. [*Note 1*: Without this, applying a `reverse_view` over a `drop_while_view` would have quadratic iteration complexity. — *end note*]
