# 25.11.8 `destroy` [specialized.destroy]

[Contents](../README.md) · up: [25.11 Specialized `<memory>` algorithms \[specialized.algorithms\]](specialized.algorithms.md) · prev: [25.11.7 `construct_at` \[specialized.construct\]](specialized.construct.md) · next: [25.12 C library algorithms \[alg.c.library\]](alg.c.library.md)

```cpp
template<class T>
  constexpr void destroy_at(T* location);
namespace ranges {
  template<destructible T>
    constexpr void destroy_at(T* location) noexcept;
}
```

**1** *Effects:*

- If `T` is an array type, equivalent to `destroy(begin(*location), end(*location))`.
- Otherwise, equivalent to `location->~T()`.

```cpp
template<class NoThrowForwardIterator>
  constexpr void destroy(NoThrowForwardIterator first, NoThrowForwardIterator last);
```

**2** *Effects:* Equivalent to:

```cpp
for (; first != last; ++first)
  destroy_at(addressof(*first));
```

```cpp
namespace ranges {
  template<no-throw-input-iterator I, no-throw-sentinel<I> S>
      requires destructible<iter_value_t<I>>
    constexpr I destroy(I first, S last) noexcept;
  template<no-throw-input-range R>
      requires destructible<range_value_t<R>>
    constexpr borrowed_iterator_t<R> destroy(R&& r) noexcept;
}
```

**3** *Effects:* Equivalent to:

```cpp
for (; first != last; ++first)
  destroy_at(addressof(*first));
return first;
```

```cpp
template<class NoThrowForwardIterator, class Size>
  constexpr NoThrowForwardIterator destroy_n(NoThrowForwardIterator first, Size n);
```

**4** *Effects:* Equivalent to:

```cpp
for (; n > 0; (void)++first, --n)
  destroy_at(addressof(*first));
return first;
```

```cpp
namespace ranges {
  template<no-throw-input-iterator I>
      requires destructible<iter_value_t<I>>
    constexpr I destroy_n(I first, iter_difference_t<I> n) noexcept;
}
```

**5** *Effects:* Equivalent to:

```cpp
return destroy(counted_iterator(first, n), default_sentinel).base();
```
