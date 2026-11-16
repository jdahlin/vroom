# 25.8.7.3 `make_heap` [make.heap]

[Contents](../README.md) · up: [25.8.7 Heap operations \[alg.heap.operations\]](alg.heap.operations.md) · prev: [25.8.7.2 `pop_heap` \[pop.heap\]](pop.heap.md) · next: [25.8.7.4 `sort_heap` \[sort.heap\]](sort.heap.md)

```cpp
template<class RandomAccessIterator>
  constexpr void make_heap(RandomAccessIterator first, RandomAccessIterator last);

template<class RandomAccessIterator, class Compare>
  constexpr void make_heap(RandomAccessIterator first, RandomAccessIterator last,
                           Compare comp);

template<random_access_iterator I, sentinel_for<I> S, class Comp = ranges::less,
         class Proj = identity>
  requires sortable<I, Comp, Proj>
  constexpr I
    ranges::make_heap(I first, S last, Comp comp = {}, Proj proj = {});
template<random_access_range R, class Comp = ranges::less, class Proj = identity>
  requires sortable<iterator_t<R>, Comp, Proj>
  constexpr borrowed_iterator_t<R>
    ranges::make_heap(R&& r, Comp comp = {}, Proj proj = {});
```

**1** Let `comp` be `less{}` and `proj` be `identity{}` for the overloads with no parameters by those names.

**2** *Preconditions:* For the overloads in namespace `std`, the type of `*first` meets the *Cpp17MoveConstructible* ([Table 28](utility.arg.requirements.md#tab:cpp17.moveconstructible)) and *Cpp17MoveAssignable* ([Table 30](utility.arg.requirements.md#tab:cpp17.moveassignable)) requirements.

**3** *Effects:* Constructs a heap with respect to `comp` and `proj` out of the range `[first, last)`.

**4** *Returns:* `last` for the overloads in namespace `ranges`.

**5** *Complexity:* At most 3(`last - first`) comparisons and twice as many projections.
