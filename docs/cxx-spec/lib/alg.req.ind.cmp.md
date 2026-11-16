# 23.3.7.5 Concept `indirectly_comparable` [alg.req.ind.cmp]

[Contents](../README.md) · up: [23.3.7 Common algorithm requirements \[alg.req\]](alg.req.md) · prev: [23.3.7.4 Concept `indirectly_swappable` \[alg.req.ind.swap\]](alg.req.ind.swap.md) · next: [23.3.7.6 Concept `permutable` \[alg.req.permutable\]](alg.req.permutable.md)

**1** The `indirectly_comparable` concept specifies the common requirements of algorithms that compare values from two different sequences.

```cpp
template<class I1, class I2, class R, class P1 = identity,
         class P2 = identity>
  concept indirectly_comparable =
    indirect_binary_predicate<R, projected<I1, P1>, projected<I2, P2>>;
```
