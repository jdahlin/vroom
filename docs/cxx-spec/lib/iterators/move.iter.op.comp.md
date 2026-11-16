# 23.5.3.7 Comparisons [move.iter.op.comp]

[Contents](../../README.md) · up: [23.5.3 Move iterators and sentinels \[move.iterators\]](move.iterators.md) · prev: [23.5.3.6 Navigation \[move.iter.nav\]](move.iter.nav.md) · next: [23.5.3.8 Non-member functions \[move.iter.nonmember\]](move.iter.nonmember.md)

```cpp
template<class Iterator1, class Iterator2>
  constexpr bool operator==(const move_iterator<Iterator1>& x,
                            const move_iterator<Iterator2>& y);
template<sentinel_for<Iterator> S>
  friend constexpr bool operator==(const move_iterator& x,
                                   const move_sentinel<S>& y);
```

**1** *Constraints:* `x.base() == y.base()` is well-formed and convertible to `bool`.

**2** *Returns:* `x.base() == y.base()`.

```cpp
template<class Iterator1, class Iterator2>
constexpr bool operator<(const move_iterator<Iterator1>& x, const move_iterator<Iterator2>& y);
```

**3** *Constraints:* `x.base() < y.base()` is well-formed and convertible to `bool`.

**4** *Returns:* `x.base() < y.base()`.

```cpp
template<class Iterator1, class Iterator2>
constexpr bool operator>(const move_iterator<Iterator1>& x, const move_iterator<Iterator2>& y);
```

**5** *Constraints:* `y.base() < x.base()` is well-formed and convertible to `bool`.

**6** *Returns:* `y < x`.

```cpp
template<class Iterator1, class Iterator2>
constexpr bool operator<=(const move_iterator<Iterator1>& x, const move_iterator<Iterator2>& y);
```

**7** *Constraints:* `y.base() < x.base()` is well-formed and convertible to `bool`.

**8** *Returns:* `!(y < x)`.

```cpp
template<class Iterator1, class Iterator2>
constexpr bool operator>=(const move_iterator<Iterator1>& x, const move_iterator<Iterator2>& y);
```

**9** *Constraints:* `x.base() < y.base()` is well-formed and convertible to `bool`.

**10** *Returns:* `!(x < y)`.

```cpp
template<class Iterator1, three_way_comparable_with<Iterator1> Iterator2>
  constexpr compare_three_way_result_t<Iterator1, Iterator2>
    operator<=>(const move_iterator<Iterator1>& x,
                const move_iterator<Iterator2>& y);
```

**11** *Returns:* `x.base() <=> y.base()`.
