# 23.5.1.8 Non-member functions [reverse.iter.nonmember]

[Contents](../../README.md) · up: [23.5.1 Reverse iterators \[reverse.iterators\]](reverse.iterators.md) · prev: [23.5.1.7 Comparisons \[reverse.iter.cmp\]](reverse.iter.cmp.md) · next: [23.5.2 Insert iterators \[insert.iterators\]](insert.iterators.md)

```cpp
template<class Iterator1, class Iterator2>
  constexpr auto operator-(
    const reverse_iterator<Iterator1>& x,
    const reverse_iterator<Iterator2>& y) -> decltype(y.base() - x.base());
```

**1** *Returns:* `y.base() - x.base()`.

```cpp
template<class Iterator>
  constexpr reverse_iterator<Iterator> operator+(
    typename reverse_iterator<Iterator>::difference_type n,
    const reverse_iterator<Iterator>& x);
```

**2** *Returns:* `reverse_iterator<Iterator>(x.base() - n)`.

```cpp
friend constexpr iter_rvalue_reference_t<Iterator>
  iter_move(const reverse_iterator& i) noexcept(see below);
```

**3** *Effects:* Equivalent to:

```cpp
auto tmp = i.base();
return ranges::iter_move(--tmp);
```

**4** *Remarks:* The expression in `noexcept` is equivalent to:

```cpp
is_nothrow_copy_constructible_v<Iterator> &&
noexcept(ranges::iter_move(--declval<Iterator&>()))
```

```cpp
template<indirectly_swappable<Iterator> Iterator2>
  friend constexpr void
    iter_swap(const reverse_iterator& x,
              const reverse_iterator<Iterator2>& y) noexcept(see below);
```

**5** *Effects:* Equivalent to:

```cpp
auto xtmp = x.base();
auto ytmp = y.base();
ranges::iter_swap(--xtmp, --ytmp);
```

**6** *Remarks:* The expression in `noexcept` is equivalent to:

```cpp
is_nothrow_copy_constructible_v<Iterator> &&
is_nothrow_copy_constructible_v<Iterator2> &&
noexcept(ranges::iter_swap(--declval<Iterator&>(), --declval<Iterator2&>()))
```

```cpp
template<class Iterator>
  constexpr reverse_iterator<Iterator> make_reverse_iterator(Iterator i);
```

**7** *Returns:* `reverse_iterator<Iterator>(i)`.
