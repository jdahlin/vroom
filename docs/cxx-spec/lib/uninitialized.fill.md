# 25.11.6 `uninitialized_fill` [uninitialized.fill]

[Contents](../README.md) · up: [25.11 Specialized `<memory>` algorithms \[specialized.algorithms\]](specialized.algorithms.md) · prev: [25.11.5 `uninitialized_move` \[uninitialized.move\]](uninitialized.move.md) · next: [25.11.7 `construct_at` \[specialized.construct\]](specialized.construct.md)

```cpp
template<class NoThrowForwardIterator, class T>
  void uninitialized_fill(NoThrowForwardIterator first, NoThrowForwardIterator last, const T& x);
```

**1** *Effects:* Equivalent to:

```cpp
for (; first != last; ++first)
  ::new (voidify(*first))
    typename iterator_traits<NoThrowForwardIterator>::value_type(x);
```

```cpp
namespace ranges {
  template<no-throw-forward-iterator I, no-throw-sentinel<I> S, class T>
      requires constructible_from<iter_value_t<I>, const T&>
    I uninitialized_fill(I first, S last, const T& x);
  template<no-throw-forward-range R, class T>
      requires constructible_from<range_value_t<R>, const T&>
    borrowed_iterator_t<R> uninitialized_fill(R&& r, const T& x);
}
```

*Effects:* Equivalent to:

```cpp
for (; first != last; ++first) {
  ::new (voidify(*first)) remove_reference_t<iter_reference_t<I>>(x);
}
return first;
```

```cpp
template<class NoThrowForwardIterator, class Size, class T>
  NoThrowForwardIterator uninitialized_fill_n(NoThrowForwardIterator first, Size n, const T& x);
```

**2** *Effects:* Equivalent to:

```cpp
for (; n--; ++first)
  ::new (voidify(*first))
    typename iterator_traits<NoThrowForwardIterator>::value_type(x);
return first;
```

```cpp
namespace ranges {
  template<no-throw-forward-iterator I, class T>
      requires constructible_from<iter_value_t<I>, const T&>
    I uninitialized_fill_n(I first, iter_difference_t<I> n, const T& x);
}
```

**3** *Effects:* Equivalent to:

```cpp
return uninitialized_fill(counted_iterator(first, n), default_sentinel, x).base();
```
