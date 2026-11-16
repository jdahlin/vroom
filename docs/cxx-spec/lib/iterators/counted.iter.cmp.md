# 23.5.6.6 Comparisons [counted.iter.cmp]

[Contents](../../README.md) · up: [23.5.6 Counted iterators \[iterators.counted\]](iterators.counted.md) · prev: [23.5.6.5 Navigation \[counted.iter.nav\]](counted.iter.nav.md) · next: [23.5.6.7 Customizations \[counted.iter.cust\]](counted.iter.cust.md)

```cpp
template<common_with<I> I2>
  friend constexpr bool operator==(
    const counted_iterator& x, const counted_iterator<I2>& y);
```

**1** *Preconditions:* `x` and `y` refer to elements of the same sequence ([[counted.iterator]](counted.iterator.md)).

**2** *Effects:* Equivalent to: `return x.length == y.length;`

```cpp
friend constexpr bool operator==(
  const counted_iterator& x, default_sentinel_t);
```

**3** *Effects:* Equivalent to: `return x.length == 0;`

```cpp
template<common_with<I> I2>
  friend constexpr strong_ordering operator<=>(
    const counted_iterator& x, const counted_iterator<I2>& y);
```

**4** *Preconditions:* `x` and `y` refer to elements of the same sequence ([[counted.iterator]](counted.iterator.md)).

**5** *Effects:* Equivalent to: `return y.length <=> x.length;`

**6** [*Note 1*: The argument order in the *Effects:* element is reversed because `length` counts down, not up. — *end note*]
