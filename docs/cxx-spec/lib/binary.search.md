# 25.8.3.4 `binary_search` [binary.search]

[Contents](../README.md) · up: [25.8.3 Binary search \[alg.binary.search\]](alg.binary.search.md) · prev: [25.8.3.3 `equal_range` \[equal.range\]](equal.range.md) · next: [25.8.4 Partitions \[alg.partitions\]](alg.partitions.md)

```cpp
template<class ForwardIterator, class T>
  constexpr bool
    binary_search(ForwardIterator first, ForwardIterator last,
                  const T& value);

template<class ForwardIterator, class T, class Compare>
  constexpr bool
    binary_search(ForwardIterator first, ForwardIterator last,
                  const T& value, Compare comp);

template<forward_iterator I, sentinel_for<I> S, class T, class Proj = identity,
         indirect_strict_weak_order<const T*, projected<I, Proj>> Comp = ranges::less>
  constexpr bool ranges::binary_search(I first, S last, const T& value, Comp comp = {},
                                       Proj proj = {});
template<forward_range R, class T, class Proj = identity,
         indirect_strict_weak_order<const T*, projected<iterator_t<R>, Proj>> Comp =
           ranges::less>
  constexpr bool ranges::binary_search(R&& r, const T& value, Comp comp = {},
                                       Proj proj = {});
```

**1** Let `comp` be `less{}` and `proj` be `identity{}` for overloads with no parameters by those names.

**2** *Preconditions:* The elements `e` of `[first, last)` are partitioned with respect to the expressions `bool(invoke(comp, invoke(proj, e), value))` and `!bool(invoke(comp, value, invoke(proj, e)))`. Also, for all elements `e` of `[first, last)`, `bool(comp(e, value))` implies `!bool(comp(value, e))` for the overloads in namespace `std`.

**3** *Returns:* `true` if and only if for some iterator `i` in the range `[first, last)`, `!bool(invoke(comp, invoke(proj, *i), value)) && !bool(invoke(comp, value, invoke(proj, *i)))` is `true`.

**4** *Complexity:* At most log<sub>2</sub>(`last - first`) + 𝑂(1) comparisons and projections.
