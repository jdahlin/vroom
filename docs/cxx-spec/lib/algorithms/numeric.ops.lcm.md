# 25.10.14 Least common multiple [numeric.ops.lcm]

[Contents](../../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.13 Greatest common divisor \[numeric.ops.gcd\]](numeric.ops.gcd.md) · next: [25.10.15 Midpoint \[numeric.ops.midpoint\]](numeric.ops.midpoint.md)

```cpp
template<class M, class N>
  constexpr common_type_t<M,N> lcm(M m, N n);
```

**1** *Mandates:* `M` and `N` both are integer types and not *cv* `bool`.

**2** *Preconditions:* |`m`| and |`n`| are representable as a value of `common_type_t<M, N>`. The least common multiple of |`m`| and |`n`| is representable as a value of type `common_type_t<M,N>`.

**3** *Returns:* Zero when either `m` or `n` is zero. Otherwise, returns the least common multiple of |`m`| and |`n`|.

**4** *Throws:* Nothing.
