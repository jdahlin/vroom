# 25.8.7.2 `pop_heap` [pop.heap]

[Contents](../../README.md) · up: [25.8.7 Heap operations \[alg.heap.operations\]](alg.heap.operations.md) · prev: [25.8.7.1 `push_heap` \[push.heap\]](push.heap.md) · next: [25.8.7.3 `make_heap` \[make.heap\]](make.heap.md)

```cpp
template<class RandomAccessIterator>
  constexpr void pop_heap(RandomAccessIterator first, RandomAccessIterator last);

template<class RandomAccessIterator, class Compare>
  constexpr void pop_heap(RandomAccessIterator first, RandomAccessIterator last,
                          Compare comp);

template<random_access_iterator I, sentinel_for<I> S, class Comp = ranges::less,
         class Proj = identity>
  requires sortable<I, Comp, Proj>
  constexpr I
    ranges::pop_heap(I first, S last, Comp comp = {}, Proj proj = {});
template<random_access_range R, class Comp = ranges::less, class Proj = identity>
  requires sortable<iterator_t<R>, Comp, Proj>
  constexpr borrowed_iterator_t<R>
    ranges::pop_heap(R&& r, Comp comp = {}, Proj proj = {});
```

**1** Let `comp` be `less{}` and `proj` be `identity{}` for the overloads with no parameters by those names.

**2** *Preconditions:* The range `[first, last)` is a valid non-empty heap with respect to `comp` and `proj`. For the overloads in namespace `std`, `RandomAccessIterator` meets the *Cpp17ValueSwappable* requirements ([[swappable.requirements]](../library/swappable.requirements.md)) and the type of `*first` meets the *Cpp17MoveConstructible* ([Table 28](../library/utility.arg.requirements.md#tab:cpp17.moveconstructible)) and *Cpp17MoveAssignable* ([Table 30](../library/utility.arg.requirements.md#tab:cpp17.moveassignable)) requirements.

**3** *Effects:* Swaps the value in the location `first` with the value in the location `last - 1` and makes `[first, last - 1)` into a heap with respect to `comp` and `proj`.

**4** *Returns:* `last` for the overloads in namespace `ranges`.

**5** *Complexity:* At most 2 log(`last - first`) comparisons and twice as many projections.
