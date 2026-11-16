# 23.5.1.7 Comparisons [reverse.iter.cmp]

[Contents](../../README.md) · up: [23.5.1 Reverse iterators \[reverse.iterators\]](reverse.iterators.md) · prev: [23.5.1.6 Navigation \[reverse.iter.nav\]](reverse.iter.nav.md) · next: [23.5.1.8 Non-member functions \[reverse.iter.nonmember\]](reverse.iter.nonmember.md)

```cpp
template<class Iterator1, class Iterator2>
  constexpr bool operator==(
    const reverse_iterator<Iterator1>& x,
    const reverse_iterator<Iterator2>& y);
```

**1** *Constraints:* `x.base() == y.base()` is well-formed and convertible to `bool`.

**2** *Returns:* `x.base() == y.base()`.

```cpp
template<class Iterator1, class Iterator2>
  constexpr bool operator!=(
    const reverse_iterator<Iterator1>& x,
    const reverse_iterator<Iterator2>& y);
```

**3** *Constraints:* `x.base() != y.base()` is well-formed and convertible to `bool`.

**4** *Returns:* `x.base() != y.base()`.

```cpp
template<class Iterator1, class Iterator2>
  constexpr bool operator<(
    const reverse_iterator<Iterator1>& x,
    const reverse_iterator<Iterator2>& y);
```

**5** *Constraints:* `x.base() > y.base()` is well-formed and convertible to `bool`.

**6** *Returns:* `x.base() > y.base()`.

```cpp
template<class Iterator1, class Iterator2>
  constexpr bool operator>(
    const reverse_iterator<Iterator1>& x,
    const reverse_iterator<Iterator2>& y);
```

**7** *Constraints:* `x.base() < y.base()` is well-formed and convertible to `bool`.

**8** *Returns:* `x.base() < y.base()`.

```cpp
template<class Iterator1, class Iterator2>
  constexpr bool operator<=(
    const reverse_iterator<Iterator1>& x,
    const reverse_iterator<Iterator2>& y);
```

**9** *Constraints:* `x.base() >= y.base()` is well-formed and convertible to `bool`.

**10** *Returns:* `x.base() >= y.base()`.

```cpp
template<class Iterator1, class Iterator2>
  constexpr bool operator>=(
    const reverse_iterator<Iterator1>& x,
    const reverse_iterator<Iterator2>& y);
```

**11** *Constraints:* `x.base() <= y.base()` is well-formed and convertible to `bool`.

**12** *Returns:* `x.base() <= y.base()`.

```cpp
template<class Iterator1, three_way_comparable_with<Iterator1> Iterator2>
  constexpr compare_three_way_result_t<Iterator1, Iterator2>
    operator<=>(const reverse_iterator<Iterator1>& x,
                const reverse_iterator<Iterator2>& y);
```

**13** *Returns:* `y.base() <=> x.base()`.

**14** [*Note 1*: The argument order in the *Returns:* element is reversed because this is a reverse iterator. — *end note*]
