# 25.8.1.3 `partial_sort` [partial.sort]

[Contents](../README.md) · up: [25.8.1 Sorting \[alg.sort\]](alg.sort.md) · prev: [25.8.1.2 `stable_sort` \[stable.sort\]](stable.sort.md) · next: [25.8.1.4 `partial_sort_copy` \[partial.sort.copy\]](partial.sort.copy.md)

```cpp
template<class RandomAccessIterator>
  constexpr void partial_sort(RandomAccessIterator first,
                              RandomAccessIterator middle,
                              RandomAccessIterator last);
template<class ExecutionPolicy, class RandomAccessIterator>
  void partial_sort(ExecutionPolicy&& exec,
                    RandomAccessIterator first,
                    RandomAccessIterator middle,
                    RandomAccessIterator last);

template<class RandomAccessIterator, class Compare>
  constexpr void partial_sort(RandomAccessIterator first,
                              RandomAccessIterator middle,
                              RandomAccessIterator last,
                              Compare comp);
template<class ExecutionPolicy, class RandomAccessIterator, class Compare>
  void partial_sort(ExecutionPolicy&& exec,
                    RandomAccessIterator first,
                    RandomAccessIterator middle,
                    RandomAccessIterator last,
                    Compare comp);

template<random_access_iterator I, sentinel_for<I> S, class Comp = ranges::less,
         class Proj = identity>
  requires sortable<I, Comp, Proj>
  constexpr I
    ranges::partial_sort(I first, I middle, S last, Comp comp = {}, Proj proj = {});
```

**1** Let `comp` be `less{}` and `proj` be `identity{}` for the overloads with no parameters by those names.

**2** *Preconditions:* `[first, middle)` and `[middle, last)` are valid ranges. For the overloads in namespace `std`, `RandomAccessIterator` meets the *Cpp17ValueSwappable* requirements ([[swappable.requirements]](swappable.requirements.md)) and the type of `*first` meets the *Cpp17MoveConstructible* ([Table 28](utility.arg.requirements.md#tab:cpp17.moveconstructible)) and *Cpp17MoveAssignable* ([Table 30](utility.arg.requirements.md#tab:cpp17.moveassignable)) requirements.

**3** *Effects:* Places the first `middle - first` elements from the range `[first, last)` as sorted with respect to `comp` and `proj` into the range `[first, middle)`. The rest of the elements in the range `[middle, last)` are placed in an unspecified order.

**4** *Returns:* `last` for the overload in namespace `ranges`.

**5** *Complexity:* Approximately `(last - first) * log(middle - first)` comparisons, and twice as many projections.

```cpp
template<random_access_range R, class Comp = ranges::less, class Proj = identity>
  requires sortable<iterator_t<R>, Comp, Proj>
  constexpr borrowed_iterator_t<R>
    ranges::partial_sort(R&& r, iterator_t<R> middle, Comp comp = {}, Proj proj = {});
```

**6** *Effects:* Equivalent to:

```cpp
return ranges::partial_sort(ranges::begin(r), middle, ranges::end(r), comp, proj);
```
