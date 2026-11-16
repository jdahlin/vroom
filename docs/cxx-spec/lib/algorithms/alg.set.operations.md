# 25.8.6 Set operations on sorted structures [alg.set.operations]

[Contents](../../README.md) · up: [25.8 Sorting and related operations \[alg.sorting\]](alg.sorting.md) · prev: [25.8.5 Merge \[alg.merge\]](alg.merge.md) · next: [25.8.6.1 `includes` \[includes\]](includes.md)

**1** This subclause defines all the basic set operations on sorted structures. They also work with `multiset`s ([[multiset]](../containers/multiset.md)) containing multiple copies of equivalent elements. The semantics of the set operations are generalized to `multiset`s in a standard way by defining `set_union` to contain the maximum number of occurrences of every element, `set_intersection` to contain the minimum, and so on.

## Subclauses

- [25.8.6.1 `includes` \[includes\]](includes.md)
- [25.8.6.2 `set_union` \[set.union\]](set.union.md)
- [25.8.6.3 `set_intersection` \[set.intersection\]](set.intersection.md)
- [25.8.6.4 `set_difference` \[set.difference\]](set.difference.md)
- [25.8.6.5 `set_symmetric_difference` \[set.symmetric.difference\]](set.symmetric.difference.md)
