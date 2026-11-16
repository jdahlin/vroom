# 25.8.1.5 `is_sorted` [is.sorted]

[Contents](../README.md) · up: [25.8.1 Sorting \[alg.sort\]](alg.sort.md) · prev: [25.8.1.4 `partial_sort_copy` \[partial.sort.copy\]](partial.sort.copy.md) · next: [25.8.2 Nth element \[alg.nth.element\]](alg.nth.element.md)

```cpp
template<class ForwardIterator>
  constexpr bool is_sorted(ForwardIterator first, ForwardIterator last);
```

**1** *Effects:* Equivalent to: `return is_sorted_until(first, last) == last;`

```cpp
template<class ExecutionPolicy, class ForwardIterator>
  bool is_sorted(ExecutionPolicy&& exec,
                 ForwardIterator first, ForwardIterator last);
```

**2** *Effects:* Equivalent to:

```cpp
return is_sorted_until(std::forward<ExecutionPolicy>(exec), first, last) == last;
```

```cpp
template<class ForwardIterator, class Compare>
  constexpr bool is_sorted(ForwardIterator first, ForwardIterator last,
                           Compare comp);
```

**3** *Effects:* Equivalent to: `return is_sorted_until(first, last, comp) == last;`

```cpp
template<class ExecutionPolicy, class ForwardIterator, class Compare>
  bool is_sorted(ExecutionPolicy&& exec,
                 ForwardIterator first, ForwardIterator last,
                 Compare comp);
```

**4** *Effects:* Equivalent to:

```cpp
return is_sorted_until(std::forward<ExecutionPolicy>(exec), first, last, comp) == last;
```

```cpp
template<forward_iterator I, sentinel_for<I> S, class Proj = identity,
         indirect_strict_weak_order<projected<I, Proj>> Comp = ranges::less>
  constexpr bool ranges::is_sorted(I first, S last, Comp comp = {}, Proj proj = {});
template<forward_range R, class Proj = identity,
         indirect_strict_weak_order<projected<iterator_t<R>, Proj>> Comp = ranges::less>
  constexpr bool ranges::is_sorted(R&& r, Comp comp = {}, Proj proj = {});
```

**5** *Effects:* Equivalent to: `return ranges::is_sorted_until(first, last, comp, proj) == last;`

```cpp
template<class ForwardIterator>
  constexpr ForwardIterator
    is_sorted_until(ForwardIterator first, ForwardIterator last);
template<class ExecutionPolicy, class ForwardIterator>
  ForwardIterator
    is_sorted_until(ExecutionPolicy&& exec,
                    ForwardIterator first, ForwardIterator last);

template<class ForwardIterator, class Compare>
  constexpr ForwardIterator
    is_sorted_until(ForwardIterator first, ForwardIterator last,
                    Compare comp);
template<class ExecutionPolicy, class ForwardIterator, class Compare>
  ForwardIterator
    is_sorted_until(ExecutionPolicy&& exec,
                    ForwardIterator first, ForwardIterator last,
                    Compare comp);

template<forward_iterator I, sentinel_for<I> S, class Proj = identity,
         indirect_strict_weak_order<projected<I, Proj>> Comp = ranges::less>
  constexpr I ranges::is_sorted_until(I first, S last, Comp comp = {}, Proj proj = {});
template<forward_range R, class Proj = identity,
         indirect_strict_weak_order<projected<iterator_t<R>, Proj>> Comp = ranges::less>
  constexpr borrowed_iterator_t<R>
    ranges::is_sorted_until(R&& r, Comp comp = {}, Proj proj = {});
```

**6** Let `comp` be `less{}` and `proj` be `identity{}` for the overloads with no parameters by those names.

**7** *Returns:* The last iterator `i` in `[first, last]` for which the range `[first, i)` is sorted with respect to `comp` and `proj`.

**8** *Complexity:* Linear.
