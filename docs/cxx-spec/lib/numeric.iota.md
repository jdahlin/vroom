# 25.10.12 Iota [numeric.iota]

[Contents](../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.11 Adjacent difference \[adjacent.difference\]](adjacent.difference.md) · next: [25.10.13 Greatest common divisor \[numeric.ops.gcd\]](numeric.ops.gcd.md)

```cpp
template<class ForwardIterator, class T>
  constexpr void iota(ForwardIterator first, ForwardIterator last, T value);
```

**1** *Mandates:* `T` is convertible to `ForwardIterator`’s value type. The expression `++val`, where `val` has type `T`, is well-formed.

**2** *Effects:* For each element referred to by the iterator `i` in the range `[first, last)`, assigns `*i = value` and increments `value` as if by `++value`.

**3** *Complexity:* Exactly `last - first` increments and assignments.
