# 23.5.6.4 Element access [counted.iter.elem]

[Contents](../README.md) · up: [23.5.6 Counted iterators \[iterators.counted\]](iterators.counted.md) · prev: [23.5.6.3 Accessors \[counted.iter.access\]](counted.iter.access.md) · next: [23.5.6.5 Navigation \[counted.iter.nav\]](counted.iter.nav.md)

```cpp
constexpr decltype(auto) operator*();
constexpr decltype(auto) operator*() const
  requires dereferenceable<const I>;
```

**1** *Effects:* Equivalent to: `return *current;`

```cpp
constexpr decltype(auto) operator[](iter_difference_t<I> n) const
  requires random_access_iterator<I>;
```

**2** *Preconditions:* `n < length`.

**3** *Effects:* Equivalent to: `return current[n];`
