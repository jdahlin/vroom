# 23.3.7.4 Concept `indirectly_swappable` [alg.req.ind.swap]

[Contents](../README.md) · up: [23.3.7 Common algorithm requirements \[alg.req\]](alg.req.md) · prev: [23.3.7.3 Concept `indirectly_copyable` \[alg.req.ind.copy\]](alg.req.ind.copy.md) · next: [23.3.7.5 Concept `indirectly_comparable` \[alg.req.ind.cmp\]](alg.req.ind.cmp.md)

**1** The `indirectly_swappable` concept specifies a swappable relationship between the values referenced by two `indirectly_readable` types.

```cpp
template<class I1, class I2 = I1>
  concept indirectly_swappable =
    indirectly_readable<I1> && indirectly_readable<I2> &&
    requires(const I1 i1, const I2 i2) {
      ranges::iter_swap(i1, i1);
      ranges::iter_swap(i2, i2);
      ranges::iter_swap(i1, i2);
      ranges::iter_swap(i2, i1);
    };
```
