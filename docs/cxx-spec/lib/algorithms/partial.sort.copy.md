# 25.8.1.4 `partial_sort_copy` [partial.sort.copy]

[Contents](../../README.md) · up: [25.8.1 Sorting \[alg.sort\]](alg.sort.md) · prev: [25.8.1.3 `partial_sort` \[partial.sort\]](partial.sort.md) · next: [25.8.1.5 `is_sorted` \[is.sorted\]](is.sorted.md)

```cpp
template<class InputIterator, class RandomAccessIterator>
  constexpr RandomAccessIterator
    partial_sort_copy(InputIterator first, InputIterator last,
                      RandomAccessIterator result_first,
                      RandomAccessIterator result_last);
template<class ExecutionPolicy, class ForwardIterator, class RandomAccessIterator>
  RandomAccessIterator
    partial_sort_copy(ExecutionPolicy&& exec,
                      ForwardIterator first, ForwardIterator last,
                      RandomAccessIterator result_first,
                      RandomAccessIterator result_last);

template<class InputIterator, class RandomAccessIterator,
         class Compare>
  constexpr RandomAccessIterator
    partial_sort_copy(InputIterator first, InputIterator last,
                      RandomAccessIterator result_first,
                      RandomAccessIterator result_last,
                      Compare comp);
template<class ExecutionPolicy, class ForwardIterator, class RandomAccessIterator,
         class Compare>
  RandomAccessIterator
    partial_sort_copy(ExecutionPolicy&& exec,
                      ForwardIterator first, ForwardIterator last,
                      RandomAccessIterator result_first,
                      RandomAccessIterator result_last,
                      Compare comp);

template<input_iterator I1, sentinel_for<I1> S1, random_access_iterator I2, sentinel_for<I2> S2,
         class Comp = ranges::less, class Proj1 = identity, class Proj2 = identity>
  requires indirectly_copyable<I1, I2> && sortable<I2, Comp, Proj2> &&
           indirect_strict_weak_order<Comp, projected<I1, Proj1>, projected<I2, Proj2>>
  constexpr ranges::partial_sort_copy_result<I1, I2>
    ranges::partial_sort_copy(I1 first, S1 last, I2 result_first, S2 result_last,
                              Comp comp = {}, Proj1 proj1 = {}, Proj2 proj2 = {});
template<input_range R1, random_access_range R2, class Comp = ranges::less,
         class Proj1 = identity, class Proj2 = identity>
  requires indirectly_copyable<iterator_t<R1>, iterator_t<R2>> &&
           sortable<iterator_t<R2>, Comp, Proj2> &&
           indirect_strict_weak_order<Comp, projected<iterator_t<R1>, Proj1>,
                                      projected<iterator_t<R2>, Proj2>>
  constexpr ranges::partial_sort_copy_result<borrowed_iterator_t<R1>, borrowed_iterator_t<R2>>
    ranges::partial_sort_copy(R1&& r, R2&& result_r, Comp comp = {},
                              Proj1 proj1 = {}, Proj2 proj2 = {});
```

**1** Let *N* be min(`last - first`, `result_last - result_first`). Let `comp` be `less{}`, and `proj1` and `proj2` be `identity{}` for the overloads with no parameters by those names.

**2** *Mandates:* For the overloads in namespace `std`, the expression `*first` is writable ([[iterator.requirements.general]](../iterators/iterator.requirements.general.md)) to `result_first`.

**3** *Preconditions:* For the overloads in namespace `std`, `RandomAccessIterator` meets the *Cpp17ValueSwappable* requirements ([[swappable.requirements]](../library/swappable.requirements.md)), the type of `*result_first` meets the *Cpp17MoveConstructible* ([Table 28](../library/utility.arg.requirements.md#tab:cpp17.moveconstructible)) and *Cpp17MoveAssignable* ([Table 30](../library/utility.arg.requirements.md#tab:cpp17.moveassignable)) requirements.

**4** *Preconditions:* For iterators `a1` and `b1` in `[first, last)`, and iterators `x2` and `y2` in `[result_first, result_last)`, after evaluating the assignment `*y2 = *b1`, let *E* be the value of

```cpp
bool(invoke(comp, invoke(proj1, *a1), invoke(proj2, *y2))).
```

Then, after evaluating the assignment `*x2 = *a1`, *E* is equal to

```cpp
bool(invoke(comp, invoke(proj2, *x2), invoke(proj2, *y2))).
```

[*Note 1*: Writing a value from the input range into the output range does not affect how it is ordered by `comp` and `proj1` or `proj2`. — *end note*]

**5** *Effects:* Places the first *N* elements as sorted with respect to `comp` and `proj2` into the range `[result_first, result_first + N)`.

**6** *Returns:*

- `result_first + N` for the overloads in namespace `std`.
- `{last, result_first + N}` for the overloads in namespace `ranges`.

**7** *Complexity:* Approximately `(last - first) * log N` comparisons, and twice as many projections.
