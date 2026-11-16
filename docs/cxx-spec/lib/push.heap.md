# 25.8.7.1 `push_heap` [push.heap]

[Contents](../README.md) · up: [25.8.7 Heap operations \[alg.heap.operations\]](alg.heap.operations.md) · prev: [25.8.7 Heap operations \[alg.heap.operations\]](alg.heap.operations.md) · next: [25.8.7.2 `pop_heap` \[pop.heap\]](pop.heap.md)

```cpp
template<class RandomAccessIterator>
  constexpr void push_heap(RandomAccessIterator first, RandomAccessIterator last);

template<class RandomAccessIterator, class Compare>
  constexpr void push_heap(RandomAccessIterator first, RandomAccessIterator last,
                           Compare comp);

template<random_access_iterator I, sentinel_for<I> S, class Comp = ranges::less,
         class Proj = identity>
  requires sortable<I, Comp, Proj>
  constexpr I
    ranges::push_heap(I first, S last, Comp comp = {}, Proj proj = {});
template<random_access_range R, class Comp = ranges::less, class Proj = identity>
  requires sortable<iterator_t<R>, Comp, Proj>
  constexpr borrowed_iterator_t<R>
    ranges::push_heap(R&& r, Comp comp = {}, Proj proj = {});
```

**1** Let `comp` be `less{}` and `proj` be `identity{}` for the overloads with no parameters by those names.

**2** *Preconditions:* The range `[first, last - 1)` is a valid heap with respect to `comp` and `proj`. For the overloads in namespace `std`, the type of `*first` meets the *Cpp17MoveConstructible* requirements ([Table 28](utility.arg.requirements.md#tab:cpp17.moveconstructible)) and the *Cpp17MoveAssignable* requirements ([Table 30](utility.arg.requirements.md#tab:cpp17.moveassignable)).

**3** *Effects:* Places the value in the location `last - 1` into the resulting heap `[first, last)`.

**4** *Returns:* `last` for the overloads in namespace `ranges`.

**5** *Complexity:* At most log(`last - first`) comparisons and twice as many projections.
