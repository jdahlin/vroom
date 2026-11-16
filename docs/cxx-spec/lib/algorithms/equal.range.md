# 25.8.3.3 `equal_range` [equal.range]

[Contents](../../README.md) · up: [25.8.3 Binary search \[alg.binary.search\]](alg.binary.search.md) · prev: [25.8.3.2 `upper_bound` \[upper.bound\]](upper.bound.md) · next: [25.8.3.4 `binary_search` \[binary.search\]](binary.search.md)

```cpp
template<class ForwardIterator, class T>
  constexpr pair<ForwardIterator, ForwardIterator>
    equal_range(ForwardIterator first,
                ForwardIterator last, const T& value);

template<class ForwardIterator, class T, class Compare>
  constexpr pair<ForwardIterator, ForwardIterator>
    equal_range(ForwardIterator first,
                ForwardIterator last, const T& value,
                Compare comp);

template<forward_iterator I, sentinel_for<I> S, class T, class Proj = identity,
         indirect_strict_weak_order<const T*, projected<I, Proj>> Comp = ranges::less>
  constexpr subrange<I>
    ranges::equal_range(I first, S last, const T& value, Comp comp = {}, Proj proj = {});
template<forward_range R, class T, class Proj = identity,
         indirect_strict_weak_order<const T*, projected<iterator_t<R>, Proj>> Comp =
           ranges::less>
  constexpr borrowed_subrange_t<R>
    ranges::equal_range(R&& r, const T& value, Comp comp = {}, Proj proj = {});
```

**1** Let `comp` be `less{}` and `proj` be `identity{}` for overloads with no parameters by those names.

**2** *Preconditions:* The elements `e` of `[first, last)` are partitioned with respect to the expressions `bool(invoke(comp, invoke(proj, e), value))` and `!bool(invoke(comp, value, invoke(proj, e)))`. Also, for all elements `e` of `[first, last)`, `bool(comp(e, value))` implies `!bool(comp(value, e))` for the overloads in namespace `std`.

**3** *Returns:*

- For the overloads in namespace `std`:

  ```cpp
  {lower_bound(first, last, value, comp),
   upper_bound(first, last, value, comp)}
  ```

- For the overloads in namespace `ranges`:

  ```cpp
  {ranges::lower_bound(first, last, value, comp, proj),
   ranges::upper_bound(first, last, value, comp, proj)}
  ```

**4** *Complexity:* At most 2 \* log<sub>2</sub>(`last - first`) + 𝑂(1) comparisons and projections.
