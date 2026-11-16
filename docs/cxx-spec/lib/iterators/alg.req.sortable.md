# 23.3.7.8 Concept `sortable` [alg.req.sortable]

[Contents](../../README.md) · up: [23.3.7 Common algorithm requirements \[alg.req\]](alg.req.md) · prev: [23.3.7.7 Concept `mergeable` \[alg.req.mergeable\]](alg.req.mergeable.md) · next: [23.4 Iterator primitives \[iterator.primitives\]](iterator.primitives.md)

**1** The `sortable` concept specifies the common requirements of algorithms that permute sequences into ordered sequences (e.g., `sort`).

```cpp
template<class I, class R = ranges::less, class P = identity>
  concept sortable =
    permutable<I> &&
    indirect_strict_weak_order<R, projected<I, P>>;
```
