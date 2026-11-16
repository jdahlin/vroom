# 25.8.6.1 `includes` [includes]

[Contents](../README.md) · up: [25.8.6 Set operations on sorted structures \[alg.set.operations\]](alg.set.operations.md) · prev: [25.8.6 Set operations on sorted structures \[alg.set.operations\]](alg.set.operations.md) · next: [25.8.6.2 `set_union` \[set.union\]](set.union.md)

```cpp
template<class InputIterator1, class InputIterator2>
  constexpr bool includes(InputIterator1 first1, InputIterator1 last1,
                          InputIterator2 first2, InputIterator2 last2);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2>
  bool includes(ExecutionPolicy&& exec,
                ForwardIterator1 first1, ForwardIterator1 last1,
                ForwardIterator2 first2, ForwardIterator2 last2);

template<class InputIterator1, class InputIterator2, class Compare>
  constexpr bool includes(InputIterator1 first1, InputIterator1 last1,
                          InputIterator2 first2, InputIterator2 last2,
                          Compare comp);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2, class Compare>
  bool includes(ExecutionPolicy&& exec,
                ForwardIterator1 first1, ForwardIterator1 last1,
                ForwardIterator2 first2, ForwardIterator2 last2,
                Compare comp);

template<input_iterator I1, sentinel_for<I1> S1, input_iterator I2, sentinel_for<I2> S2,
         class Proj1 = identity, class Proj2 = identity,
         indirect_strict_weak_order<projected<I1, Proj1>,
                                    projected<I2, Proj2>> Comp = ranges::less>
  constexpr bool ranges::includes(I1 first1, S1 last1, I2 first2, S2 last2, Comp comp = {},
                                  Proj1 proj1 = {}, Proj2 proj2 = {});
template<input_range R1, input_range R2, class Proj1 = identity,
         class Proj2 = identity,
         indirect_strict_weak_order<projected<iterator_t<R1>, Proj1>,
                                    projected<iterator_t<R2>, Proj2>> Comp = ranges::less>
  constexpr bool ranges::includes(R1&& r1, R2&& r2, Comp comp = {},
                                  Proj1 proj1 = {}, Proj2 proj2 = {});
```

**1** Let `comp` be `less{}`, `proj1` be `identity{}`, and `proj2` be `identity{}`, for the overloads with no parameters by those names.

**2** *Preconditions:* The ranges `[first1, last1)` and `[first2, last2)` are sorted with respect to `comp` and `proj1` or `proj2`, respectively.

**3** *Returns:* `true` if and only if `[first2, last2)` is a subsequence of `[first1, last1)`. [*Note 1*: A sequence *S* is a subsequence of another sequence *T* if *S* can be obtained from *T* by removing some, all, or none of *T*’s elements and keeping the remaining elements in the same order. — *end note*]

**4** *Complexity:* At most `2 * ((last1 - first1) + (last2 - first2)) - 1` comparisons and applications of each projection.
