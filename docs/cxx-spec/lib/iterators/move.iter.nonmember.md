# 23.5.3.8 Non-member functions [move.iter.nonmember]

[Contents](../../README.md) · up: [23.5.3 Move iterators and sentinels \[move.iterators\]](move.iterators.md) · prev: [23.5.3.7 Comparisons \[move.iter.op.comp\]](move.iter.op.comp.md) · next: [23.5.3.9 Class template `move_sentinel` \[move.sentinel\]](move.sentinel.md)

```cpp
template<class Iterator1, class Iterator2>
  constexpr auto operator-(const move_iterator<Iterator1>& x,
                           const move_iterator<Iterator2>& y)
    -> decltype(x.base() - y.base());
template<sized_sentinel_for<Iterator> S>
  friend constexpr iter_difference_t<Iterator>
    operator-(const move_sentinel<S>& x, const move_iterator& y);
template<sized_sentinel_for<Iterator> S>
  friend constexpr iter_difference_t<Iterator>
    operator-(const move_iterator& x, const move_sentinel<S>& y);
```

**1** *Returns:* `x.base() - y.base()`.

```cpp
template<class Iterator>
  constexpr move_iterator<Iterator>
    operator+(iter_difference_t<Iterator> n, const move_iterator<Iterator>& x);
```

**2** *Constraints:* `x + n` is well-formed and has type `Iterator`.

**3** *Returns:* `x + n`.

```cpp
friend constexpr iter_rvalue_reference_t<Iterator>
  iter_move(const move_iterator& i)
    noexcept(noexcept(ranges::iter_move(i.current)));
```

**4** *Effects:* Equivalent to: `return ranges::iter_move(i.current);`

```cpp
template<indirectly_swappable<Iterator> Iterator2>
  friend constexpr void
    iter_swap(const move_iterator& x, const move_iterator<Iterator2>& y)
      noexcept(noexcept(ranges::iter_swap(x.current, y.current)));
```

**5** *Effects:* Equivalent to: `ranges::iter_swap(x.current, y.current)`.

```cpp
template<class Iterator>
constexpr move_iterator<Iterator> make_move_iterator(Iterator i);
```

**6** *Returns:* `move_iterator<Iterator>(std::move(i))`.
