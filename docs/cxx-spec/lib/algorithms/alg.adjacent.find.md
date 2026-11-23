# 25.6.8 Adjacent find [alg.adjacent.find]

[Contents](../../README.md) · up: [25.6 Non-modifying sequence operations \[alg.nonmodifying\]](alg.nonmodifying.md) · prev: [25.6.7 Find first \[alg.find.first.of\]](alg.find.first.of.md) · next: [25.6.9 Count \[alg.count\]](alg.count.md)

```cpp
template<class ForwardIterator>
  constexpr ForwardIterator
    adjacent_find(ForwardIterator first, ForwardIterator last);
template<class ExecutionPolicy, class ForwardIterator>
  ForwardIterator
    adjacent_find(ExecutionPolicy&& exec,
                  ForwardIterator first, ForwardIterator last);

template<class ForwardIterator, class BinaryPredicate>
  constexpr ForwardIterator
    adjacent_find(ForwardIterator first, ForwardIterator last,
                  BinaryPredicate pred);
template<class ExecutionPolicy, class ForwardIterator, class BinaryPredicate>
  ForwardIterator
    adjacent_find(ExecutionPolicy&& exec,
                  ForwardIterator first, ForwardIterator last,
                  BinaryPredicate pred);

template<forward_iterator I, sentinel_for<I> S, class Proj = identity,
         indirect_binary_predicate<projected<I, Proj>,
                                   projected<I, Proj>> Pred = ranges::equal_to>
  constexpr I ranges::adjacent_find(I first, S last, Pred pred = {}, Proj proj = {});
template<forward_range R, class Proj = identity,
         indirect_binary_predicate<projected<iterator_t<R>, Proj>,
                                   projected<iterator_t<R>, Proj>> Pred = ranges::equal_to>
  constexpr borrowed_iterator_t<R> ranges::adjacent_find(R&& r, Pred pred = {}, Proj proj = {});
```

**1** Let *E* be:

- `*i == *(i + 1)` for the overloads with no parameter `pred`;
- `pred(*i, *(i + 1)) != false` for the overloads with a parameter `pred` and no parameter `proj`;
- `bool(invoke(pred, invoke(proj, *i), invoke(proj, *(i + 1))))` for the overloads with both parameters `pred` and `proj`.

**2** *Returns:* The first iterator `i` such that both `i` and `i + 1` are in the range `[first, last)` for which *E* holds. Returns `last` if no such iterator is found.

**3** *Complexity:* For the overloads with no `ExecutionPolicy`, exactly

$$\min(\tcode{(i - first) + 1}, \ \tcode{(last - first) - 1})$$

applications of the corresponding predicate, where `i` is `adjacent_find`’s return value. For the overloads with an `ExecutionPolicy`, 𝑂(`last - first`) applications of the corresponding predicate, and no more than twice as many applications of any projection.
