# 23.5.6.7 Customizations [counted.iter.cust]

[Contents](../README.md) · up: [23.5.6 Counted iterators \[iterators.counted\]](iterators.counted.md) · prev: [23.5.6.6 Comparisons \[counted.iter.cmp\]](counted.iter.cmp.md) · next: [23.5.7 Unreachable sentinel \[unreachable.sentinels\]](unreachable.sentinels.md)

```cpp
friend constexpr iter_rvalue_reference_t<I>
  iter_move(const counted_iterator& i)
    noexcept(noexcept(ranges::iter_move(i.current)))
    requires input_iterator<I>;
```

**1** *Effects:* Equivalent to: `return ranges::iter_move(i.current);`

```cpp
template<indirectly_swappable<I> I2>
  friend constexpr void
    iter_swap(const counted_iterator& x, const counted_iterator<I2>& y)
      noexcept(noexcept(ranges::iter_swap(x.current, y.current)));
```

**2** *Effects:* Equivalent to `ranges::iter_swap(x.current, y.current)`.
