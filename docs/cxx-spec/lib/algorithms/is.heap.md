# 25.8.7.5 `is_heap` [is.heap]

[Contents](../../README.md) · up: [25.8.7 Heap operations \[alg.heap.operations\]](alg.heap.operations.md) · prev: [25.8.7.4 `sort_heap` \[sort.heap\]](sort.heap.md) · next: [25.8.8 Minimum and maximum \[alg.min.max\]](alg.min.max.md)

```cpp
template<class RandomAccessIterator>
  constexpr bool is_heap(RandomAccessIterator first, RandomAccessIterator last);
```

**1** *Effects:* Equivalent to: `return is_heap_until(first, last) == last;`

```cpp
template<class ExecutionPolicy, class RandomAccessIterator>
  bool is_heap(ExecutionPolicy&& exec,
               RandomAccessIterator first, RandomAccessIterator last);
```

**2** *Effects:* Equivalent to:

```cpp
return is_heap_until(std::forward<ExecutionPolicy>(exec), first, last) == last;
```

```cpp
template<class RandomAccessIterator, class Compare>
  constexpr bool is_heap(RandomAccessIterator first, RandomAccessIterator last,
                         Compare comp);
```

**3** *Effects:* Equivalent to: `return is_heap_until(first, last, comp) == last;`

```cpp
template<class ExecutionPolicy, class RandomAccessIterator, class Compare>
  bool is_heap(ExecutionPolicy&& exec,
               RandomAccessIterator first, RandomAccessIterator last,
               Compare comp);
```

**4** *Effects:* Equivalent to:

```cpp
return is_heap_until(std::forward<ExecutionPolicy>(exec), first, last, comp) == last;
```

```cpp
template<random_access_iterator I, sentinel_for<I> S, class Proj = identity,
         indirect_strict_weak_order<projected<I, Proj>> Comp = ranges::less>
  constexpr bool ranges::is_heap(I first, S last, Comp comp = {}, Proj proj = {});
template<random_access_range R, class Proj = identity,
         indirect_strict_weak_order<projected<iterator_t<R>, Proj>> Comp = ranges::less>
  constexpr bool ranges::is_heap(R&& r, Comp comp = {}, Proj proj = {});
```

**5** *Effects:* Equivalent to: `return ranges::is_heap_until(first, last, comp, proj) == last;`

```cpp
template<class RandomAccessIterator>
  constexpr RandomAccessIterator
    is_heap_until(RandomAccessIterator first, RandomAccessIterator last);
template<class ExecutionPolicy, class RandomAccessIterator>
  RandomAccessIterator
    is_heap_until(ExecutionPolicy&& exec,
                  RandomAccessIterator first, RandomAccessIterator last);

template<class RandomAccessIterator, class Compare>
  constexpr RandomAccessIterator
    is_heap_until(RandomAccessIterator first, RandomAccessIterator last,
                  Compare comp);
template<class ExecutionPolicy, class RandomAccessIterator, class Compare>
  RandomAccessIterator
    is_heap_until(ExecutionPolicy&& exec,
                  RandomAccessIterator first, RandomAccessIterator last,
                  Compare comp);

template<random_access_iterator I, sentinel_for<I> S, class Proj = identity,
         indirect_strict_weak_order<projected<I, Proj>> Comp = ranges::less>
  constexpr I ranges::is_heap_until(I first, S last, Comp comp = {}, Proj proj = {});
template<random_access_range R, class Proj = identity,
         indirect_strict_weak_order<projected<iterator_t<R>, Proj>> Comp = ranges::less>
  constexpr borrowed_iterator_t<R>
    ranges::is_heap_until(R&& r, Comp comp = {}, Proj proj = {});
```

**6** Let `comp` be `less{}` and `proj` be `identity{}` for the overloads with no parameters by those names.

**7** *Returns:* The last iterator `i` in `[first, last]` for which the range `[first, i)` is a heap with respect to `comp` and `proj`.

**8** *Complexity:* Linear.
