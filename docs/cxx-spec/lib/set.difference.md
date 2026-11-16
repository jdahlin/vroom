# 25.8.6.4 `set_difference` [set.difference]

[Contents](../README.md) · up: [25.8.6 Set operations on sorted structures \[alg.set.operations\]](alg.set.operations.md) · prev: [25.8.6.3 `set_intersection` \[set.intersection\]](set.intersection.md) · next: [25.8.6.5 `set_symmetric_difference` \[set.symmetric.difference\]](set.symmetric.difference.md)

```cpp
template<class InputIterator1, class InputIterator2,
         class OutputIterator>
  constexpr OutputIterator
    set_difference(InputIterator1 first1, InputIterator1 last1,
                   InputIterator2 first2, InputIterator2 last2,
                   OutputIterator result);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
         class ForwardIterator>
  ForwardIterator
    set_difference(ExecutionPolicy&& exec,
                   ForwardIterator1 first1, ForwardIterator1 last1,
                   ForwardIterator2 first2, ForwardIterator2 last2,
                   ForwardIterator result);

template<class InputIterator1, class InputIterator2,
         class OutputIterator, class Compare>
  constexpr OutputIterator
    set_difference(InputIterator1 first1, InputIterator1 last1,
                   InputIterator2 first2, InputIterator2 last2,
                   OutputIterator result, Compare comp);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
         class ForwardIterator, class Compare>
  ForwardIterator
    set_difference(ExecutionPolicy&& exec,
                   ForwardIterator1 first1, ForwardIterator1 last1,
                   ForwardIterator2 first2, ForwardIterator2 last2,
                   ForwardIterator result, Compare comp);

template<input_iterator I1, sentinel_for<I1> S1, input_iterator I2, sentinel_for<I2> S2,
         weakly_incrementable O, class Comp = ranges::less,
         class Proj1 = identity, class Proj2 = identity>
  requires mergeable<I1, I2, O, Comp, Proj1, Proj2>
  constexpr ranges::set_difference_result<I1, O>
    ranges::set_difference(I1 first1, S1 last1, I2 first2, S2 last2, O result,
                           Comp comp = {}, Proj1 proj1 = {}, Proj2 proj2 = {});
template<input_range R1, input_range R2, weakly_incrementable O,
         class Comp = ranges::less, class Proj1 = identity, class Proj2 = identity>
  requires mergeable<iterator_t<R1>, iterator_t<R2>, O, Comp, Proj1, Proj2>
  constexpr ranges::set_difference_result<borrowed_iterator_t<R1>, O>
    ranges::set_difference(R1&& r1, R2&& r2, O result,
                           Comp comp = {}, Proj1 proj1 = {}, Proj2 proj2 = {});
```

**1** Let `comp` be `less{}`, and `proj1` and `proj2` be `identity{}` for the overloads with no parameters by those names.

**2** *Preconditions:* The ranges `[first1, last1)` and `[first2, last2)` are sorted with respect to `comp` and `proj1` or `proj2`, respectively. The resulting range does not overlap with either of the original ranges.

**3** *Effects:* Copies the elements of the range `[first1, last1)` which are not present in the range `[first2, last2)` to the range beginning at `result`. The elements in the constructed range are sorted.

**4** *Returns:* Let `result_last` be the end of the constructed range. Returns

- `result_last` for the overloads in namespace `std`.
- `{last1, result_last}` for the overloads in namespace `ranges`.

**5** *Complexity:* At most `2 * ((last1 - first1) + (last2 - first2)) - 1` comparisons and applications of each projection.

**6** *Remarks:* If `[first1, last1)` contains *m* elements that are equivalent to each other and `[first2, last2)` contains *n* elements that are equivalent to them, the last max(*m* - *n*, 0) elements from `[first1, last1)` is copied to the output range, in order.
