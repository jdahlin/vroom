# 25.6.12 Is permutation [alg.is.permutation]

[Contents](../README.md) · up: [25.6 Non-modifying sequence operations \[alg.nonmodifying\]](alg.nonmodifying.md) · prev: [25.6.11 Equal \[alg.equal\]](alg.equal.md) · next: [25.6.13 Search \[alg.search\]](alg.search.md)

```cpp
template<class ForwardIterator1, class ForwardIterator2>
  constexpr bool is_permutation(ForwardIterator1 first1, ForwardIterator1 last1,
                                ForwardIterator2 first2);
template<class ForwardIterator1, class ForwardIterator2,
         class BinaryPredicate>
  constexpr bool is_permutation(ForwardIterator1 first1, ForwardIterator1 last1,
                                ForwardIterator2 first2, BinaryPredicate pred);
template<class ForwardIterator1, class ForwardIterator2>
  constexpr bool is_permutation(ForwardIterator1 first1, ForwardIterator1 last1,
                                ForwardIterator2 first2, ForwardIterator2 last2);
template<class ForwardIterator1, class ForwardIterator2,
         class BinaryPredicate>
  constexpr bool is_permutation(ForwardIterator1 first1, ForwardIterator1 last1,
                                ForwardIterator2 first2, ForwardIterator2 last2,
                                BinaryPredicate pred);
```

**1** *Mandates:* `ForwardIterator1` and `ForwardIterator2` have the same value type.

**2** *Preconditions:* The comparison function is an equivalence relation.

**3** *Remarks:* If `last2` was not given in the argument list, it denotes `first2 + (last1 - first1)` below.

**4** *Returns:* If `last1 - first1 != last2 - first2`, return `false`. Otherwise return `true` if there exists a permutation of the elements in the range `[first2, first2 + (last1 - first1))`, beginning with `ForwardIterator2 begin`, such that `equal(first1, last1, begin)` returns `true` or `equal(first1, last1, begin, pred)` returns `true`; otherwise, returns `false`.

**5** *Complexity:* No applications of the corresponding predicate if `ForwardIterator1` and `ForwardIterator2` meet the requirements of random access iterators and `last1 - first1 != last2 - first2`. Otherwise, exactly `last1 - first1` applications of the corresponding predicate if `equal(first1, last1, first2, last2)` would return `true` if `pred` was not given in the argument list or `equal(first1, last1, first2, last2, pred)` would return `true` if `pred` was given in the argument list; otherwise, at worst 𝑂(*N*<sup>2</sup>), where *N* has the value `last1 - first1`.

```cpp
template<forward_iterator I1, sentinel_for<I1> S1, forward_iterator I2,
         sentinel_for<I2> S2, class Proj1 = identity, class Proj2 = identity,
         indirect_equivalence_relation<projected<I1, Proj1>,
                                       projected<I2, Proj2>> Pred = ranges::equal_to>
  constexpr bool ranges::is_permutation(I1 first1, S1 last1, I2 first2, S2 last2,
                                        Pred pred = {},
                                        Proj1 proj1 = {}, Proj2 proj2 = {});
template<forward_range R1, forward_range R2,
         class Proj1 = identity, class Proj2 = identity,
         indirect_equivalence_relation<projected<iterator_t<R1>, Proj1>,
                                       projected<iterator_t<R2>, Proj2>> Pred = ranges::equal_to>
  constexpr bool ranges::is_permutation(R1&& r1, R2&& r2, Pred pred = {},
                                        Proj1 proj1 = {}, Proj2 proj2 = {});
```

**6** *Returns:* If `last1 - first1 != last2 - first2`, return `false`. Otherwise return `true` if there exists a permutation of the elements in the range `[first2, last2)`, bounded by `[pfirst, plast)`, such that `ranges::equal(first1, last1, pfirst, plast, pred, proj1, proj2)` returns `true`; otherwise, returns `false`.

**7** *Complexity:* No applications of the corresponding predicate and projections if:

- `S1` and `I1` model `sized_sentinel_for<S1, I1>`,
- `S2` and `I2` model `sized_sentinel_for<S2, I2>`, and
- `last1 - first1 != last2 - first2`.

Otherwise, exactly `last1 - first1` applications of the corresponding predicate and projections if `ranges::equal(first1, last1, first2, last2, pred, proj1, proj2)` would return `true`; otherwise, at worst 𝑂(*N*<sup>2</sup>), where *N* has the value `last1 - first1`.
