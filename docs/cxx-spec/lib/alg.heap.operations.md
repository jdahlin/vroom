# 25.8.7 Heap operations [alg.heap.operations]

[Contents](../README.md) · up: [25.8 Sorting and related operations \[alg.sorting\]](alg.sorting.md) · prev: [25.8.6.5 `set_symmetric_difference` \[set.symmetric.difference\]](set.symmetric.difference.md) · next: [25.8.7.1 `push_heap` \[push.heap\]](push.heap.md)

**1** A random access range `[a, b)` is a *heap with respect to `comp` and `proj`* for a comparator and projection `comp` and `proj` if its elements are organized such that:

- With `N = b - a`, for all *i*, 0 &lt; *i* &lt; *N*, `bool(invoke(comp, invoke(proj, a[\left \lfloor{\frac{i - 1}{2}}\right \rfloor]), invoke(proj, a[i])))` is `false`.
- `*a` may be removed by `pop_heap`, or a new element added by `push_heap`, in 𝑂(log *N*) time.

**2** These properties make heaps useful as priority queues.

**3** `make_heap` converts a range into a heap and `sort_heap` turns a heap into a sorted sequence.

## Subclauses

- [25.8.7.1 `push_heap` \[push.heap\]](push.heap.md)
- [25.8.7.2 `pop_heap` \[pop.heap\]](pop.heap.md)
- [25.8.7.3 `make_heap` \[make.heap\]](make.heap.md)
- [25.8.7.4 `sort_heap` \[sort.heap\]](sort.heap.md)
- [25.8.7.5 `is_heap` \[is.heap\]](is.heap.md)
