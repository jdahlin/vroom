# 25.11.5 `uninitialized_move` [uninitialized.move]

[Contents](../../README.md) · up: [25.11 Specialized `<memory>` algorithms \[specialized.algorithms\]](specialized.algorithms.md) · prev: [25.11.4 `uninitialized_copy` \[uninitialized.copy\]](uninitialized.copy.md) · next: [25.11.6 `uninitialized_fill` \[uninitialized.fill\]](uninitialized.fill.md)

```cpp
template<class InputIterator, class NoThrowForwardIterator>
  NoThrowForwardIterator uninitialized_move(InputIterator first, InputIterator last,
                                            NoThrowForwardIterator result);
```

**1** *Preconditions:* `result` + `[0, (last - first))` does not overlap with `[first, last)`.

**2** *Effects:* Equivalent to:

```cpp
for (; first != last; (void)++result, ++first)
  ::new (voidify(*result))
    typename iterator_traits<NoThrowForwardIterator>::value_type(std::move(*first));
return result;
```

```cpp
namespace ranges {
  template<input_iterator I, sentinel_for<I> S1,
           no-throw-forward-iterator O, no-throw-sentinel<O> S2>
      requires constructible_from<iter_value_t<O>, iter_rvalue_reference_t<I>>
    uninitialized_move_result<I, O>
      uninitialized_move(I ifirst, S1 ilast, O ofirst, S2 olast);
  template<input_range IR, no-throw-forward-range OR>
      requires constructible_from<range_value_t<OR>, range_rvalue_reference_t<IR>>
    uninitialized_move_result<borrowed_iterator_t<IR>, borrowed_iterator_t<OR>>
      uninitialized_move(IR&& in_range, OR&& out_range);
}
```

**3** *Preconditions:* `[ofirst, olast)` does not overlap with `[ifirst, ilast)`.

**4** *Effects:* Equivalent to:

```cpp
for (; ifirst != ilast && ofirst != olast; ++ofirst, (void)++ifirst) {
  ::new (voidify(*ofirst))
    remove_reference_t<iter_reference_t<O>>(ranges::iter_move(ifirst));
}
return {std::move(ifirst), ofirst};
```

**5** [*Note 1*: If an exception is thrown, some objects in the range `[first, last)` are left in a valid, but unspecified state. — *end note*]

```cpp
template<class InputIterator, class Size, class NoThrowForwardIterator>
  pair<InputIterator, NoThrowForwardIterator>
    uninitialized_move_n(InputIterator first, Size n, NoThrowForwardIterator result);
```

**6** *Preconditions:* `result` + `[0, n)` does not overlap with `first` + `[0, n)`.

**7** *Effects:* Equivalent to:

```cpp
for (; n > 0; ++result, (void) ++first, --n)
  ::new (voidify(*result))
    typename iterator_traits<NoThrowForwardIterator>::value_type(std::move(*first));
return {first, result};
```

```cpp
namespace ranges {
  template<input_iterator I, no-throw-forward-iterator O, no-throw-sentinel<O> S>
      requires constructible_from<iter_value_t<O>, iter_rvalue_reference_t<I>>
    uninitialized_move_n_result<I, O>
      uninitialized_move_n(I ifirst, iter_difference_t<I> n, O ofirst, S olast);
}
```

**8** *Preconditions:* `[ofirst, olast)` does not overlap with `ifirst` + `[0, n)`.

**9** *Effects:* Equivalent to:

```cpp
auto t = uninitialized_move(counted_iterator(ifirst, n),
                            default_sentinel, ofirst, olast);
return {std::move(t.in).base(), t.out};
```

**10** [*Note 2*: If an exception is thrown, some objects in the range `first` + `[0, n)` are left in a valid but unspecified state. — *end note*]
