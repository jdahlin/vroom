# 25.8.3.1 `lower_bound` [lower.bound]

[Contents](../README.md) · up: [25.8.3 Binary search \[alg.binary.search\]](alg.binary.search.md) · prev: [25.8.3 Binary search \[alg.binary.search\]](alg.binary.search.md) · next: [25.8.3.2 `upper_bound` \[upper.bound\]](upper.bound.md)

```cpp
template<class ForwardIterator, class T>
  constexpr ForwardIterator
    lower_bound(ForwardIterator first, ForwardIterator last,
                const T& value);

template<class ForwardIterator, class T, class Compare>
  constexpr ForwardIterator
    lower_bound(ForwardIterator first, ForwardIterator last,
                const T& value, Compare comp);

template<forward_iterator I, sentinel_for<I> S, class T, class Proj = identity,
         indirect_strict_weak_order<const T*, projected<I, Proj>> Comp = ranges::less>
  constexpr I ranges::lower_bound(I first, S last, const T& value, Comp comp = {},
                                  Proj proj = {});
template<forward_range R, class T, class Proj = identity,
         indirect_strict_weak_order<const T*, projected<iterator_t<R>, Proj>> Comp =
           ranges::less>
  constexpr borrowed_iterator_t<R>
    ranges::lower_bound(R&& r, const T& value, Comp comp = {}, Proj proj = {});
```

**1** Let `comp` be `less{}` and `proj` be `identity{}` for overloads with no parameters by those names.

**2** *Preconditions:* The elements `e` of `[first, last)` are partitioned with respect to the expression `bool(invoke(comp, invoke(proj, e), value))`.

**3** *Returns:* The furthermost iterator `i` in the range `[first, last]` such that for every iterator `j` in the range `[first, i)`, `bool(invoke(comp, invoke(proj, *j), value))` is `true`.

**4** *Complexity:* At most log<sub>2</sub>(`last - first`) + 𝑂(1) comparisons and projections.
