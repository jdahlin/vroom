# 25.11.4 `uninitialized_copy` [uninitialized.copy]

[Contents](../../README.md) · up: [25.11 Specialized `<memory>` algorithms \[specialized.algorithms\]](specialized.algorithms.md) · prev: [25.11.3 `uninitialized_value_construct` \[uninitialized.construct.value\]](uninitialized.construct.value.md) · next: [25.11.5 `uninitialized_move` \[uninitialized.move\]](uninitialized.move.md)

```cpp
template<class InputIterator, class NoThrowForwardIterator>
  NoThrowForwardIterator uninitialized_copy(InputIterator first, InputIterator last,
                                            NoThrowForwardIterator result);
```

**1** *Preconditions:* `result` + `[0, (last - first))` does not overlap with `[first, last)`.

**2** *Effects:* Equivalent to:

```cpp
for (; first != last; ++result, (void) ++first)
  ::new (voidify(*result))
    typename iterator_traits<NoThrowForwardIterator>::value_type(*first);
```

**3** *Returns:* `result`.

```cpp
namespace ranges {
  template<input_iterator I, sentinel_for<I> S1,
           no-throw-forward-iterator O, no-throw-sentinel<O> S2>
      requires constructible_from<iter_value_t<O>, iter_reference_t<I>>
    uninitialized_copy_result<I, O>
      uninitialized_copy(I ifirst, S1 ilast, O ofirst, S2 olast);
  template<input_range IR, no-throw-forward-range OR>
      requires constructible_from<range_value_t<OR>, range_reference_t<IR>>
    uninitialized_copy_result<borrowed_iterator_t<IR>, borrowed_iterator_t<OR>>
      uninitialized_copy(IR&& in_range, OR&& out_range);
}
```

**4** *Preconditions:* `[ofirst, olast)` does not overlap with `[ifirst, ilast)`.

**5** *Effects:* Equivalent to:

```cpp
for (; ifirst != ilast && ofirst != olast; ++ofirst, (void)++ifirst) {
  ::new (voidify(*ofirst)) remove_reference_t<iter_reference_t<O>>(*ifirst);
}
return {std::move(ifirst), ofirst};
```

```cpp
template<class InputIterator, class Size, class NoThrowForwardIterator>
  NoThrowForwardIterator uninitialized_copy_n(InputIterator first, Size n,
                                              NoThrowForwardIterator result);
```

**6** *Preconditions:* `result` + `[0, n)` does not overlap with `first` + `[0, n)`.

**7** *Effects:* Equivalent to:

```cpp
for ( ; n > 0; ++result, (void) ++first, --n) {
  ::new (voidify(*result))
    typename iterator_traits<NoThrowForwardIterator>::value_type(*first);
}
```

**8** *Returns:* `result`.

```cpp
namespace ranges {
  template<input_iterator I, no-throw-forward-iterator O, no-throw-sentinel<O> S>
      requires constructible_from<iter_value_t<O>, iter_reference_t<I>>
    uninitialized_copy_n_result<I, O>
      uninitialized_copy_n(I ifirst, iter_difference_t<I> n, O ofirst, S olast);
}
```

**9** *Preconditions:* `[ofirst, olast)` does not overlap with `ifirst` + `[0, n)`.

**10** *Effects:* Equivalent to:

```cpp
auto t = uninitialized_copy(counted_iterator(ifirst, n),
                            default_sentinel, ofirst, olast);
return {std::move(t.in).base(), t.out};
```
