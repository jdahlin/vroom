# 25.8.4 Partitions [alg.partitions]

[Contents](../README.md) · up: [25.8 Sorting and related operations \[alg.sorting\]](alg.sorting.md) · prev: [25.8.3.4 `binary_search` \[binary.search\]](binary.search.md) · next: [25.8.5 Merge \[alg.merge\]](alg.merge.md)

```cpp
template<class InputIterator, class Predicate>
  constexpr bool is_partitioned(InputIterator first, InputIterator last, Predicate pred);
template<class ExecutionPolicy, class ForwardIterator, class Predicate>
  bool is_partitioned(ExecutionPolicy&& exec,
                      ForwardIterator first, ForwardIterator last, Predicate pred);

template<input_iterator I, sentinel_for<I> S, class Proj = identity,
         indirect_unary_predicate<projected<I, Proj>> Pred>
  constexpr bool ranges::is_partitioned(I first, S last, Pred pred, Proj proj = {});
template<input_range R, class Proj = identity,
         indirect_unary_predicate<projected<iterator_t<R>, Proj>> Pred>
  constexpr bool ranges::is_partitioned(R&& r, Pred pred, Proj proj = {});
```

**1** Let `proj` be `identity{}` for the overloads with no parameter named `proj`.

**2** *Returns:* `true` if and only if the elements `e` of `[first, last)` are partitioned with respect to the expression `bool(invoke(pred, invoke(proj, e)))`.

**3** *Complexity:* Linear. At most `last - first` applications of `pred` and `proj`.

```cpp
template<class ForwardIterator, class Predicate>
  constexpr ForwardIterator
    partition(ForwardIterator first, ForwardIterator last, Predicate pred);
template<class ExecutionPolicy, class ForwardIterator, class Predicate>
  ForwardIterator
    partition(ExecutionPolicy&& exec,
              ForwardIterator first, ForwardIterator last, Predicate pred);

template<permutable I, sentinel_for<I> S, class Proj = identity,
         indirect_unary_predicate<projected<I, Proj>> Pred>
  constexpr subrange<I>
    ranges::partition(I first, S last, Pred pred, Proj proj = {});
template<forward_range R, class Proj = identity,
         indirect_unary_predicate<projected<iterator_t<R>, Proj>> Pred>
  requires permutable<iterator_t<R>>
  constexpr borrowed_subrange_t<R>
    ranges::partition(R&& r, Pred pred, Proj proj = {});
```

**4** Let `proj` be `identity{}` for the overloads with no parameter named `proj` and let *E*(*x*) be `bool(invoke(pred, invoke(proj, x)))`.

**5** *Preconditions:* For the overloads in namespace `std`, `ForwardIterator` meets the *Cpp17ValueSwappable* requirements ([[swappable.requirements]](swappable.requirements.md)).

**6** *Effects:* Places all the elements `e` in `[first, last)` that satisfy *E*(`e`) before all the elements that do not.

**7** *Returns:* Let `i` be an iterator such that *E*(`*j`) is `true` for every iterator `j` in `[first, i)` and `false` for every iterator `j` in `[i, last)`. Returns:

- `i` for the overloads in namespace `std`.
- `{i, last}` for the overloads in namespace `ranges`.

**8** *Complexity:* Let *N* = `last - first`:

- For the overload with no `ExecutionPolicy`, exactly *N* applications of the predicate and projection. At most *N* / 2 swaps if the type of `first` meets the *Cpp17BidirectionalIterator* requirements for the overloads in namespace `std` or models `bidirectional_iterator` for the overloads in namespace `ranges`, and at most *N* swaps otherwise.
- For the overload with an `ExecutionPolicy`, 𝑂(*N* log *N*) swaps and 𝑂(*N*) applications of the predicate.

```cpp
template<class BidirectionalIterator, class Predicate>
  BidirectionalIterator
    stable_partition(BidirectionalIterator first, BidirectionalIterator last, Predicate pred);
template<class ExecutionPolicy, class BidirectionalIterator, class Predicate>
  BidirectionalIterator
    stable_partition(ExecutionPolicy&& exec,
                     BidirectionalIterator first, BidirectionalIterator last, Predicate pred);

template<bidirectional_iterator I, sentinel_for<I> S, class Proj = identity,
         indirect_unary_predicate<projected<I, Proj>> Pred>
  requires permutable<I>
  subrange<I> ranges::stable_partition(I first, S last, Pred pred, Proj proj = {});
template<bidirectional_range R, class Proj = identity,
         indirect_unary_predicate<projected<iterator_t<R>, Proj>> Pred>
  requires permutable<iterator_t<R>>
  borrowed_subrange_t<R> ranges::stable_partition(R&& r, Pred pred, Proj proj = {});
```

**9** Let `proj` be `identity{}` for the overloads with no parameter named `proj` and let *E*(*x*) be `bool(invoke(pred, invoke(proj, x)))`.

**10** *Preconditions:* For the overloads in namespace `std`, `BidirectionalIterator` meets the *Cpp17ValueSwappable* requirements ([[swappable.requirements]](swappable.requirements.md)) and the type of `*first` meets the *Cpp17MoveConstructible* ([Table 28](utility.arg.requirements.md#tab:cpp17.moveconstructible)) and *Cpp17MoveAssignable* ([Table 30](utility.arg.requirements.md#tab:cpp17.moveassignable)) requirements.

**11** *Effects:* Places all the elements `e` in `[first, last)` that satisfy *E*(`e`) before all the elements that do not. The relative order of the elements in both groups is preserved.

**12** *Returns:* Let `i` be an iterator such that for every iterator `j` in `[first, i)`, *E*(`*j`) is `true`, and for every iterator `j` in the range `[i, last)`, *E*(`*j`) is `false`. Returns:

- `i` for the overloads in namespace `std`.
- `{i, last}` for the overloads in namespace `ranges`.

**13** *Complexity:* Let *N* = `last - first`:

- For the overloads with no `ExecutionPolicy`, at most *N* log *N* swaps, but only 𝑂(*N*) swaps if there is enough extra memory. Exactly *N* applications of the predicate and projection.
- For the overload with an `ExecutionPolicy`, 𝑂(*N* log *N*) swaps and 𝑂(*N*) applications of the predicate.

```cpp
template<class InputIterator, class OutputIterator1,
         class OutputIterator2, class Predicate>
  constexpr pair<OutputIterator1, OutputIterator2>
    partition_copy(InputIterator first, InputIterator last,
                   OutputIterator1 out_true, OutputIterator2 out_false, Predicate pred);
template<class ExecutionPolicy, class ForwardIterator, class ForwardIterator1,
         class ForwardIterator2, class Predicate>
  pair<ForwardIterator1, ForwardIterator2>
    partition_copy(ExecutionPolicy&& exec,
                   ForwardIterator first, ForwardIterator last,
                   ForwardIterator1 out_true, ForwardIterator2 out_false, Predicate pred);

template<input_iterator I, sentinel_for<I> S, weakly_incrementable O1, weakly_incrementable O2,
         class Proj = identity, indirect_unary_predicate<projected<I, Proj>> Pred>
  requires indirectly_copyable<I, O1> && indirectly_copyable<I, O2>
  constexpr ranges::partition_copy_result<I, O1, O2>
    ranges::partition_copy(I first, S last, O1 out_true, O2 out_false, Pred pred,
                           Proj proj = {});
template<input_range R, weakly_incrementable O1, weakly_incrementable O2,
         class Proj = identity,
         indirect_unary_predicate<projected<iterator_t<R>, Proj>> Pred>
  requires indirectly_copyable<iterator_t<R>, O1> &&
           indirectly_copyable<iterator_t<R>, O2>
  constexpr ranges::partition_copy_result<borrowed_iterator_t<R>, O1, O2>
    ranges::partition_copy(R&& r, O1 out_true, O2 out_false, Pred pred, Proj proj = {});
```

**14** Let `proj` be `identity{}` for the overloads with no parameter named `proj` and let *E*(*x*) be `bool(invoke(pred, invoke(proj, x)))`.

**15** *Mandates:* For the overloads in namespace `std`, the expression `*first` is writable ([[iterator.requirements.general]](iterator.requirements.general.md)) to `out_true` and `out_false`.

**16** *Preconditions:* The input range and output ranges do not overlap.

[*Note 1*: For the overload with an `ExecutionPolicy`, there may be a performance cost if `first`’s value type does not meet the *Cpp17CopyConstructible* requirements. — *end note*]

**17** *Effects:* For each iterator `i` in `[first, last)`, copies `*i` to the output range beginning with `out_true` if `E(*i)` is `true`, or to the output range beginning with `out_false` otherwise.

**18** *Returns:* Let `o1` be the end of the output range beginning at `out_true`, and `o2` the end of the output range beginning at `out_false`. Returns

- `{o1, o2}` for the overloads in namespace `std`.
- `{last, o1, o2}` for the overloads in namespace `ranges`.

**19** *Complexity:* Exactly `last - first` applications of `pred` and `proj`.

```cpp
template<class ForwardIterator, class Predicate>
  constexpr ForwardIterator
    partition_point(ForwardIterator first, ForwardIterator last, Predicate pred);

template<forward_iterator I, sentinel_for<I> S, class Proj = identity,
         indirect_unary_predicate<projected<I, Proj>> Pred>
  constexpr I ranges::partition_point(I first, S last, Pred pred, Proj proj = {});
template<forward_range R, class Proj = identity,
         indirect_unary_predicate<projected<iterator_t<R>, Proj>> Pred>
  constexpr borrowed_iterator_t<R>
    ranges::partition_point(R&& r, Pred pred, Proj proj = {});
```

**20** Let `proj` be `identity{}` for the overloads with no parameter named `proj` and let *E*(*x*) be `bool(invoke(pred, invoke(proj, x)))`.

**21** *Preconditions:* The elements `e` of `[first, last)` are partitioned with respect to *E*(`e`).

**22** *Returns:* An iterator `mid` such that *E*(`*i`) is `true` for all iterators `i` in `[first, mid)`, and `false` for all iterators `i` in `[mid, last)`.

**23** *Complexity:* 𝑂(log(`last - first`)) applications of `pred` and `proj`.
