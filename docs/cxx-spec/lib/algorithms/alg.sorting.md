# 25.8 Sorting and related operations [alg.sorting]

[Contents](../../README.md) · up: [25 Algorithms library \[algorithms\]](algorithms.md) · prev: [25.7.14 Shift \[alg.shift\]](alg.shift.md) · next: [25.8.1 Sorting \[alg.sort\]](alg.sort.md)

**1** The operations in [[alg.sorting]](alg.sorting.md) defined directly in namespace `std` have two versions: one that takes a function object of type `Compare` and one that uses an `operator<`.

**2** `Compare` is a function object type ([[function.objects]](../utilities/function.objects.md)) that meets the requirements for a template parameter named `BinaryPredicate` ([[algorithms.requirements]](algorithms.requirements.md)). The return value of the function call operation applied to an object of type `Compare`, when contextually converted to `bool` ([[conv]](../../expr/conv.md)), yields `true` if the first argument of the call is less than the second, and `false` otherwise. `Compare comp` is used throughout for algorithms assuming an ordering relation.

**3** For all algorithms that take `Compare`, there is a version that uses `operator<` instead. That is, `comp(*i, *j) != false` defaults to `*i < *j != false`. For algorithms other than those described in [[alg.binary.search]](alg.binary.search.md), `comp` shall induce a strict weak ordering on the values.

**4** The term *strict* refers to the requirement of an irreflexive relation (`!comp(x, x)` for all `x`), and the term *weak* to requirements that are not as strong as those for a total ordering, but stronger than those for a partial ordering. If we define `equiv(a, b)` as `!comp(a, b) && !comp(b, a)`, then the requirements are that `comp` and `equiv` both be transitive relations:

- `comp(a, b) && comp(b, c)` implies `comp(a, c)`
- `equiv(a, b) && equiv(b, c)` implies `equiv(a, c)`

[*Note 1*: Under these conditions, it can be shown that

- `equiv` is an equivalence relation,
- `comp` induces a well-defined relation on the equivalence classes determined by `equiv`, and
- the induced relation is a strict total ordering.

— *end note*]

**5** A sequence is *sorted with respect to a `comp` and `proj`* for a comparator and projection `comp` and `proj` if for every iterator `i` pointing to the sequence and every non-negative integer `n` such that `i + n` is a valid iterator pointing to an element of the sequence,

```cpp
bool(invoke(comp, invoke(proj, *(i + n)), invoke(proj, *i)))
```

is `false`.

**6** A sequence `[start, finish)` is *partitioned with respect to an expression* `f(e)` if there exists an integer `n` such that for all `0 <= i < (finish - start)`, `f(*(start + i))` is `true` if and only if `i < n`.

**7** In the descriptions of the functions that deal with ordering relationships we frequently use a notion of equivalence to describe concepts such as stability. The equivalence to which we refer is not necessarily an `operator==`, but an equivalence relation induced by the strict weak ordering. That is, two elements `a` and `b` are considered equivalent if and only if `!(a < b) && !(b < a)`.

## Subclauses

- [25.8.1 Sorting \[alg.sort\]](alg.sort.md)
  - [25.8.1.1 `sort` \[sort\]](sort.md)
  - [25.8.1.2 `stable_sort` \[stable.sort\]](stable.sort.md)
  - [25.8.1.3 `partial_sort` \[partial.sort\]](partial.sort.md)
  - [25.8.1.4 `partial_sort_copy` \[partial.sort.copy\]](partial.sort.copy.md)
  - [25.8.1.5 `is_sorted` \[is.sorted\]](is.sorted.md)
- [25.8.2 Nth element \[alg.nth.element\]](alg.nth.element.md)
- [25.8.3 Binary search \[alg.binary.search\]](alg.binary.search.md)
  - [25.8.3.1 `lower_bound` \[lower.bound\]](lower.bound.md)
  - [25.8.3.2 `upper_bound` \[upper.bound\]](upper.bound.md)
  - [25.8.3.3 `equal_range` \[equal.range\]](equal.range.md)
  - [25.8.3.4 `binary_search` \[binary.search\]](binary.search.md)
- [25.8.4 Partitions \[alg.partitions\]](alg.partitions.md)
- [25.8.5 Merge \[alg.merge\]](alg.merge.md)
- [25.8.6 Set operations on sorted structures \[alg.set.operations\]](alg.set.operations.md)
  - [25.8.6.1 `includes` \[includes\]](includes.md)
  - [25.8.6.2 `set_union` \[set.union\]](set.union.md)
  - [25.8.6.3 `set_intersection` \[set.intersection\]](set.intersection.md)
  - [25.8.6.4 `set_difference` \[set.difference\]](set.difference.md)
  - [25.8.6.5 `set_symmetric_difference` \[set.symmetric.difference\]](set.symmetric.difference.md)
- [25.8.7 Heap operations \[alg.heap.operations\]](alg.heap.operations.md)
  - [25.8.7.1 `push_heap` \[push.heap\]](push.heap.md)
  - [25.8.7.2 `pop_heap` \[pop.heap\]](pop.heap.md)
  - [25.8.7.3 `make_heap` \[make.heap\]](make.heap.md)
  - [25.8.7.4 `sort_heap` \[sort.heap\]](sort.heap.md)
  - [25.8.7.5 `is_heap` \[is.heap\]](is.heap.md)
- [25.8.8 Minimum and maximum \[alg.min.max\]](alg.min.max.md)
- [25.8.9 Bounded value \[alg.clamp\]](alg.clamp.md)
- [25.8.10 Lexicographical comparison \[alg.lex.comparison\]](alg.lex.comparison.md)
- [25.8.11 Three-way comparison algorithms \[alg.three.way\]](alg.three.way.md)
- [25.8.12 Permutation generators \[alg.permutation.generators\]](alg.permutation.generators.md)
