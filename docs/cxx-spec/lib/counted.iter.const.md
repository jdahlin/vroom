# 23.5.6.2 Constructors and conversions [counted.iter.const]

[Contents](../README.md) · up: [23.5.6 Counted iterators \[iterators.counted\]](iterators.counted.md) · prev: [23.5.6.1 Class template `counted_iterator` \[counted.iterator\]](counted.iterator.md) · next: [23.5.6.3 Accessors \[counted.iter.access\]](counted.iter.access.md)

```cpp
constexpr counted_iterator(I i, iter_difference_t<I> n);
```

**1** *Preconditions:* `n >= 0`.

**2** *Effects:* Initializes `current` with `std::move(i)` and `length` with `n`.

```cpp
template<class I2>
  requires convertible_to<const I2&, I>
    constexpr counted_iterator(const counted_iterator<I2>& x);
```

**3** *Effects:* Initializes `current` with `x.current` and `length` with `x.length`.

```cpp
template<class I2>
  requires assignable_from<I&, const I2&>
    constexpr counted_iterator& operator=(const counted_iterator<I2>& x);
```

**4** *Effects:* Assigns `x.current` to `current` and `x.length` to `length`.

**5** *Returns:* `*this`.
