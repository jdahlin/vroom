# 25.8.1.2 `stable_sort` [stable.sort]

[Contents](../../README.md) · up: [25.8.1 Sorting \[alg.sort\]](alg.sort.md) · prev: [25.8.1.1 `sort` \[sort\]](sort.md) · next: [25.8.1.3 `partial_sort` \[partial.sort\]](partial.sort.md)

```cpp
template<class RandomAccessIterator>
  void stable_sort(RandomAccessIterator first, RandomAccessIterator last);
template<class ExecutionPolicy, class RandomAccessIterator>
  void stable_sort(ExecutionPolicy&& exec,
                   RandomAccessIterator first, RandomAccessIterator last);

template<class RandomAccessIterator, class Compare>
  void stable_sort(RandomAccessIterator first, RandomAccessIterator last,
                   Compare comp);
template<class ExecutionPolicy, class RandomAccessIterator, class Compare>
  void stable_sort(ExecutionPolicy&& exec,
                   RandomAccessIterator first, RandomAccessIterator last,
                   Compare comp);

template<random_access_iterator I, sentinel_for<I> S, class Comp = ranges::less,
         class Proj = identity>
  requires sortable<I, Comp, Proj>
  I ranges::stable_sort(I first, S last, Comp comp = {}, Proj proj = {});
template<random_access_range R, class Comp = ranges::less, class Proj = identity>
  requires sortable<iterator_t<R>, Comp, Proj>
  borrowed_iterator_t<R>
    ranges::stable_sort(R&& r, Comp comp = {}, Proj proj = {});
```

**1** Let `comp` be `less{}` and `proj` be `identity{}` for the overloads with no parameters by those names.

**2** *Preconditions:* For the overloads in namespace `std`, `RandomAccessIterator` meets the *Cpp17ValueSwappable* requirements ([[swappable.requirements]](../library/swappable.requirements.md)) and the type of `*first` meets the *Cpp17MoveConstructible* ([Table 28](../library/utility.arg.requirements.md#tab:cpp17.moveconstructible)) and *Cpp17MoveAssignable* ([Table 30](../library/utility.arg.requirements.md#tab:cpp17.moveassignable)) requirements.

**3** *Effects:* Sorts the elements in the range `[first, last)` with respect to `comp` and `proj`.

**4** *Returns:* `last` for the overloads in namespace `ranges`.

**5** *Complexity:* Let *N* be `last - first`. If enough extra memory is available, *N* log(*N*) comparisons. Otherwise, at most *N* log<sup>2</sup>(*N*) comparisons. In either case, twice as many projections as the number of comparisons.

**6** *Remarks:* Stable ([[algorithm.stable]](../library/algorithm.stable.md)).
