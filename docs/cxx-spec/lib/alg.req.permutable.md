# 23.3.7.6 Concept `permutable` [alg.req.permutable]

[Contents](../README.md) · up: [23.3.7 Common algorithm requirements \[alg.req\]](alg.req.md) · prev: [23.3.7.5 Concept `indirectly_comparable` \[alg.req.ind.cmp\]](alg.req.ind.cmp.md) · next: [23.3.7.7 Concept `mergeable` \[alg.req.mergeable\]](alg.req.mergeable.md)

**1** The `permutable` concept specifies the common requirements of algorithms that reorder elements in place by moving or swapping them.

```cpp
template<class I>
  concept permutable =
    forward_iterator<I> &&
    indirectly_movable_storable<I, I> &&
    indirectly_swappable<I, I>;
```
