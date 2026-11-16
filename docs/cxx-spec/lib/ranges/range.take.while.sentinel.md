# 24.7.7.3 Class template `take_while_view::sentinel` [range.take.while.sentinel]

[Contents](../../README.md) · up: [24.7.7 Take while view \[range.take.while\]](range.take.while.md) · prev: [24.7.7.2 Class template `take_while_view` \[range.take.while.view\]](range.take.while.view.md) · next: [24.7.8 Drop view \[range.drop\]](range.drop.md)

```cpp
namespace std::ranges {
  template<view V, class Pred>
    requires input_range<V> && is_object_v<Pred> &&
             indirect_unary_predicate<const Pred, iterator_t<V>>
  template<bool Const>
  class take_while_view<V, Pred>::sentinel {            // exposition only
    using Base = conditional_t<Const, const V, V>;      // exposition only

    sentinel_t<Base> end_ = sentinel_t<Base>();         // exposition only
    const Pred* pred_ = nullptr;                        // exposition only
  public:
    sentinel() = default;
    constexpr explicit sentinel(sentinel_t<Base> end, const Pred* pred);
    constexpr sentinel(sentinel<!Const> s)
      requires Const && convertible_to<sentinel_t<V>, sentinel_t<Base>>;

    constexpr sentinel_t<Base> base() const { return end_; }

    friend constexpr bool operator==(const iterator_t<Base>& x, const sentinel& y);
  };
}
```

```cpp
constexpr explicit sentinel(sentinel_t<Base> end, const Pred* pred);
```

**1** *Effects:* Initializes `end_` with `end` and `pred_` with `pred`.

```cpp
constexpr sentinel(sentinel<!Const> s)
  requires Const && convertible_to<sentinel_t<V>, sentinel_t<Base>>;
```

**2** *Effects:* Initializes `end_` with `s.end_` and `pred_` with `s.pred_`.

```cpp
friend constexpr bool operator==(const iterator_t<Base>& x, const sentinel& y);
```

**3** *Effects:* Equivalent to: `return y.end_ == x || !invoke(*y.pred_, *x);`
