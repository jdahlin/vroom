# 23.3.7.7 Concept `mergeable` [alg.req.mergeable]

[Contents](../README.md) · up: [23.3.7 Common algorithm requirements \[alg.req\]](alg.req.md) · prev: [23.3.7.6 Concept `permutable` \[alg.req.permutable\]](alg.req.permutable.md) · next: [23.3.7.8 Concept `sortable` \[alg.req.sortable\]](alg.req.sortable.md)

**1** The `mergeable` concept specifies the requirements of algorithms that merge sorted sequences into an output sequence by copying elements.

```cpp
template<class I1, class I2, class Out, class R = ranges::less,
         class P1 = identity, class P2 = identity>
  concept mergeable =
    input_iterator<I1> &&
    input_iterator<I2> &&
    weakly_incrementable<Out> &&
    indirectly_copyable<I1, Out> &&
    indirectly_copyable<I2, Out> &&
    indirect_strict_weak_order<R, projected<I1, P1>, projected<I2, P2>>;
```
