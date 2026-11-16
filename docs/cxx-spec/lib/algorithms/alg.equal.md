# 25.6.11 Equal [alg.equal]

[Contents](../../README.md) · up: [25.6 Non-modifying sequence operations \[alg.nonmodifying\]](alg.nonmodifying.md) · prev: [25.6.10 Mismatch \[mismatch\]](mismatch.md) · next: [25.6.12 Is permutation \[alg.is.permutation\]](alg.is.permutation.md)

```cpp
template<class InputIterator1, class InputIterator2>
  constexpr bool equal(InputIterator1 first1, InputIterator1 last1,
                       InputIterator2 first2);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2>
  bool equal(ExecutionPolicy&& exec,
             ForwardIterator1 first1, ForwardIterator1 last1,
             ForwardIterator2 first2);

template<class InputIterator1, class InputIterator2,
         class BinaryPredicate>
  constexpr bool equal(InputIterator1 first1, InputIterator1 last1,
                       InputIterator2 first2, BinaryPredicate pred);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
         class BinaryPredicate>
  bool equal(ExecutionPolicy&& exec,
             ForwardIterator1 first1, ForwardIterator1 last1,
             ForwardIterator2 first2, BinaryPredicate pred);

template<class InputIterator1, class InputIterator2>
  constexpr bool equal(InputIterator1 first1, InputIterator1 last1,
                       InputIterator2 first2, InputIterator2 last2);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2>
  bool equal(ExecutionPolicy&& exec,
             ForwardIterator1 first1, ForwardIterator1 last1,
             ForwardIterator2 first2, ForwardIterator2 last2);

template<class InputIterator1, class InputIterator2,
         class BinaryPredicate>
  constexpr bool equal(InputIterator1 first1, InputIterator1 last1,
                       InputIterator2 first2, InputIterator2 last2,
                       BinaryPredicate pred);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
         class BinaryPredicate>
  bool equal(ExecutionPolicy&& exec,
             ForwardIterator1 first1, ForwardIterator1 last1,
             ForwardIterator2 first2, ForwardIterator2 last2,
             BinaryPredicate pred);

template<input_iterator I1, sentinel_for<I1> S1, input_iterator I2, sentinel_for<I2> S2,
         class Pred = ranges::equal_to, class Proj1 = identity, class Proj2 = identity>
  requires indirectly_comparable<I1, I2, Pred, Proj1, Proj2>
  constexpr bool ranges::equal(I1 first1, S1 last1, I2 first2, S2 last2,
                               Pred pred = {},
                               Proj1 proj1 = {}, Proj2 proj2 = {});
template<input_range R1, input_range R2, class Pred = ranges::equal_to,
         class Proj1 = identity, class Proj2 = identity>
  requires indirectly_comparable<iterator_t<R1>, iterator_t<R2>, Pred, Proj1, Proj2>
  constexpr bool ranges::equal(R1&& r1, R2&& r2, Pred pred = {},
                               Proj1 proj1 = {}, Proj2 proj2 = {});
```

**1** Let:

- `last2` be `first2 + (last1 - first1)` for the overloads with no parameter `last2` or `r2`;

- `pred` be `equal_to{}` for the overloads with no parameter `pred`;

- *E* be:

  - `pred(*i, *(first2 + (i - first1)))` for the overloads with no parameter `proj1`;
  - `invoke(pred, invoke(proj1, *i), invoke(proj2, *(first2 + (i - first1))))` for the overloads with parameter `proj1`.

**2** *Returns:* If `last1 - first1 != last2 - first2`, return `false`. Otherwise return `true` if *E* holds for every iterator `i` in the range `[first1, last1)` Otherwise, returns `false`.

**3** *Complexity:* If the types of `first1`, `last1`, `first2`, and `last2`:

- meet the *Cpp17RandomAccessIterator* requirements ([[random.access.iterators]](../iterators/random.access.iterators.md)) for the overloads in namespace `std`;
- pairwise model `sized_sentinel_for` ([[iterator.concept.sizedsentinel]](../iterators/iterator.concept.sizedsentinel.md)) for the overloads in namespace `ranges`,

and `last1 - first1 != last2 - first2`, then no applications of the corresponding predicate and each projection; otherwise,

- For the overloads with no `ExecutionPolicy`, at most min(`last1 - first1`, `last2 - first2`) applications of the corresponding predicate and any projections.
- For the overloads with an `ExecutionPolicy`, 𝑂(min(`last1 - first1`, `last2 - first2`)) applications of the corresponding predicate.
