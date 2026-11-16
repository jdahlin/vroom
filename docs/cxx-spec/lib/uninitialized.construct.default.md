# 25.11.2 `uninitialized_default_construct` [uninitialized.construct.default]

[Contents](../README.md) · up: [25.11 Specialized `<memory>` algorithms \[specialized.algorithms\]](specialized.algorithms.md) · prev: [25.11.1 Special memory concepts \[special.mem.concepts\]](special.mem.concepts.md) · next: [25.11.3 `uninitialized_value_construct` \[uninitialized.construct.value\]](uninitialized.construct.value.md)

```cpp
template<class NoThrowForwardIterator>
  void uninitialized_default_construct(NoThrowForwardIterator first, NoThrowForwardIterator last);
```

**1** *Effects:* Equivalent to:

```cpp
for (; first != last; ++first)
  ::new (voidify(*first))
    typename iterator_traits<NoThrowForwardIterator>::value_type;
```

```cpp
namespace ranges {
  template<no-throw-forward-iterator I, no-throw-sentinel<I> S>
      requires default_initializable<iter_value_t<I>>
    I uninitialized_default_construct(I first, S last);
  template<no-throw-forward-range R>
      requires default_initializable<range_value_t<R>>
    borrowed_iterator_t<R> uninitialized_default_construct(R&& r);
}
```

**2** *Effects:* Equivalent to:

```cpp
for (; first != last; ++first)
  ::new (voidify(*first)) remove_reference_t<iter_reference_t<I>>;
return first;
```

```cpp
template<class NoThrowForwardIterator, class Size>
  NoThrowForwardIterator uninitialized_default_construct_n(NoThrowForwardIterator first, Size n);
```

**3** *Effects:* Equivalent to:

```cpp
for (; n > 0; (void)++first, --n)
  ::new (voidify(*first))
    typename iterator_traits<NoThrowForwardIterator>::value_type;
return first;
```

```cpp
namespace ranges {
  template<no-throw-forward-iterator I>
      requires default_initializable<iter_value_t<I>>
    I uninitialized_default_construct_n(I first, iter_difference_t<I> n);
}
```

**4** *Effects:* Equivalent to:

```cpp
return uninitialized_default_construct(counted_iterator(first, n),
                                       default_sentinel).base();
```
