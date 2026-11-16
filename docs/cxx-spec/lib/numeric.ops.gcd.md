# 25.10.13 Greatest common divisor [numeric.ops.gcd]

[Contents](../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.12 Iota \[numeric.iota\]](numeric.iota.md) · next: [25.10.14 Least common multiple \[numeric.ops.lcm\]](numeric.ops.lcm.md)

```cpp
template<class M, class N>
  constexpr common_type_t<M,N> gcd(M m, N n);
```

**1** *Mandates:* `M` and `N` both are integer types and not *cv* `bool`.

**2** *Preconditions:* |`m`| and |`n`| are representable as a value of `common_type_t<M, N>`. [*Note 1*: These requirements ensure, for example, that `gcd(m, m)` = |`m`| is representable as a value of type `M`. — *end note*]

**3** *Returns:* Zero when `m` and `n` are both zero. Otherwise, returns the greatest common divisor of |`m`| and |`n`|.

**4** *Throws:* Nothing.
