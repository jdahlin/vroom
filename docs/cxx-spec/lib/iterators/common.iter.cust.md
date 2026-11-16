# 23.5.4.7 Customization [common.iter.cust]

[Contents](../../README.md) · up: [23.5.4 Common iterators \[iterators.common\]](iterators.common.md) · prev: [23.5.4.6 Comparisons \[common.iter.cmp\]](common.iter.cmp.md) · next: [23.5.5 Default sentinels \[default.sentinels\]](default.sentinels.md)

```cpp
friend iter_rvalue_reference_t<I> iter_move(const common_iterator& i)
  noexcept(noexcept(ranges::iter_move(declval<const I&>())))
    requires input_iterator<I>;
```

**1** *Preconditions:* `holds_alternative<I>(v_)`.

**2** *Effects:* Equivalent to: `return ranges::iter_move(get<I>(i.v_));`

```cpp
template<indirectly_swappable<I> I2, class S2>
  friend void iter_swap(const common_iterator& x, const common_iterator<I2, S2>& y)
    noexcept(noexcept(ranges::iter_swap(declval<const I&>(), declval<const I2&>())));
```

**3** *Preconditions:* `holds_alternative<I>(x.v_)` and `holds_alternative<I2>(y.v_)` are each `true`.

**4** *Effects:* Equivalent to `ranges::iter_swap(get<I>(x.v_), get<I2>(y.v_))`.
