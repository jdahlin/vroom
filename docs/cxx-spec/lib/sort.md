# 25.8.1.1 `sort` [sort]

[Contents](../README.md) · up: [25.8.1 Sorting \[alg.sort\]](alg.sort.md) · prev: [25.8.1 Sorting \[alg.sort\]](alg.sort.md) · next: [25.8.1.2 `stable_sort` \[stable.sort\]](stable.sort.md)

```cpp
template<class RandomAccessIterator>
  constexpr void sort(RandomAccessIterator first, RandomAccessIterator last);
template<class ExecutionPolicy, class RandomAccessIterator>
  void sort(ExecutionPolicy&& exec,
            RandomAccessIterator first, RandomAccessIterator last);

template<class RandomAccessIterator, class Compare>
  constexpr void sort(RandomAccessIterator first, RandomAccessIterator last,
                      Compare comp);
template<class ExecutionPolicy, class RandomAccessIterator, class Compare>
  void sort(ExecutionPolicy&& exec,
            RandomAccessIterator first, RandomAccessIterator last,
            Compare comp);

template<random_access_iterator I, sentinel_for<I> S, class Comp = ranges::less,
         class Proj = identity>
  requires sortable<I, Comp, Proj>
  constexpr I
    ranges::sort(I first, S last, Comp comp = {}, Proj proj = {});
template<random_access_range R, class Comp = ranges::less, class Proj = identity>
  requires sortable<iterator_t<R>, Comp, Proj>
  constexpr borrowed_iterator_t<R>
    ranges::sort(R&& r, Comp comp = {}, Proj proj = {});
```

**1** Let `comp` be `less{}` and `proj` be `identity{}` for the overloads with no parameters by those names.

**2** *Preconditions:* For the overloads in namespace `std`, `RandomAccessIterator` meets the *Cpp17ValueSwappable* requirements ([[swappable.requirements]](swappable.requirements.md)) and the type of `*first` meets the *Cpp17MoveConstructible* ([Table 28](utility.arg.requirements.md#tab:cpp17.moveconstructible)) and *Cpp17MoveAssignable* ([Table 30](utility.arg.requirements.md#tab:cpp17.moveassignable)) requirements.

**3** *Effects:* Sorts the elements in the range `[first, last)` with respect to `comp` and `proj`.

**4** *Returns:* `last` for the overloads in namespace `ranges`.

**5** *Complexity:* Let *N* be `last - first`. 𝑂(*N* log *N*) comparisons and projections.
