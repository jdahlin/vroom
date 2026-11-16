# 25.8.3 Binary search [alg.binary.search]

[Contents](../README.md) · up: [25.8 Sorting and related operations \[alg.sorting\]](alg.sorting.md) · prev: [25.8.2 Nth element \[alg.nth.element\]](alg.nth.element.md) · next: [25.8.3.1 `lower_bound` \[lower.bound\]](lower.bound.md)

**1** All of the algorithms in this subclause are versions of binary search and assume that the sequence being searched is partitioned with respect to an expression formed by binding the search key to an argument of the comparison function. They work on non-random access iterators minimizing the number of comparisons, which will be logarithmic for all types of iterators. They are especially appropriate for random access iterators, because these algorithms do a logarithmic number of steps through the data structure. For non-random access iterators they execute a linear number of steps.

## Subclauses

- [25.8.3.1 `lower_bound` \[lower.bound\]](lower.bound.md)
- [25.8.3.2 `upper_bound` \[upper.bound\]](upper.bound.md)
- [25.8.3.3 `equal_range` \[equal.range\]](equal.range.md)
- [25.8.3.4 `binary_search` \[binary.search\]](binary.search.md)
