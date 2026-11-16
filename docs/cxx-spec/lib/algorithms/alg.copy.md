# 25.7.1 Copy [alg.copy]

[Contents](../../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · next: [25.7.2 Move \[alg.move\]](alg.move.md)

```cpp
template<class InputIterator, class OutputIterator>
  constexpr OutputIterator copy(InputIterator first, InputIterator last,
                                OutputIterator result);

template<input_iterator I, sentinel_for<I> S, weakly_incrementable O>
  requires indirectly_copyable<I, O>
  constexpr ranges::copy_result<I, O> ranges::copy(I first, S last, O result);
template<input_range R, weakly_incrementable O>
  requires indirectly_copyable<iterator_t<R>, O>
  constexpr ranges::copy_result<borrowed_iterator_t<R>, O> ranges::copy(R&& r, O result);
```

**1** Let *N* be `last - first`.

**2** *Preconditions:* `result` is not in the range `[first, last)`.

**3** *Effects:* Copies elements in the range `[first, last)` into the range `[result, result + N)` starting from `first` and proceeding to `last`. For each non-negative integer *n* &lt; *N*, performs `*(result + n) = *(first + n)`.

**4** *Returns:*

- `result + N` for the overload in namespace `std`.
- `{last, result + N}` for the overloads in namespace `ranges`.

**5** *Complexity:* Exactly *N* assignments.

```cpp
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2>
  ForwardIterator2 copy(ExecutionPolicy&& policy,
                        ForwardIterator1 first, ForwardIterator1 last,
                        ForwardIterator2 result);
```

**6** *Preconditions:* The ranges `[first, last)` and `[result, result + (last - first))` do not overlap.

**7** *Effects:* Copies elements in the range `[first, last)` into the range `[result, result + (last - first))`. For each non-negative integer `n < (last - first)`, performs `*(result + n) = *(first + n)`.

**8** *Returns:* `result + (last - first)`.

**9** *Complexity:* Exactly `last - first` assignments.

```cpp
template<class InputIterator, class Size, class OutputIterator>
  constexpr OutputIterator copy_n(InputIterator first, Size n,
                                  OutputIterator result);
template<class ExecutionPolicy, class ForwardIterator1, class Size, class ForwardIterator2>
  ForwardIterator2 copy_n(ExecutionPolicy&& exec,
                          ForwardIterator1 first, Size n,
                          ForwardIterator2 result);

template<input_iterator I, weakly_incrementable O>
  requires indirectly_copyable<I, O>
  constexpr ranges::copy_n_result<I, O>
    ranges::copy_n(I first, iter_difference_t<I> n, O result);
```

**10** Let *N* be max(0, `n`).

**11** *Mandates:* The type `Size` is convertible to an integral type ([[conv.integral]](../../expr/conv.integral.md), [[class.conv]](../../class/class.conv.md)).

**12** *Effects:* For each non-negative integer *i* &lt; *N*, performs `*(result + i) = *(first + i)`.

**13** *Returns:*

- `result + N` for the overloads in namespace `std`.
- `{first + N, result + N}` for the overload in namespace `ranges`.

**14** *Complexity:* Exactly *N* assignments.

```cpp
template<class InputIterator, class OutputIterator, class Predicate>
  constexpr OutputIterator copy_if(InputIterator first, InputIterator last,
                                   OutputIterator result, Predicate pred);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
         class Predicate>
  ForwardIterator2 copy_if(ExecutionPolicy&& exec,
                           ForwardIterator1 first, ForwardIterator1 last,
                           ForwardIterator2 result, Predicate pred);

template<input_iterator I, sentinel_for<I> S, weakly_incrementable O, class Proj = identity,
         indirect_unary_predicate<projected<I, Proj>> Pred>
  requires indirectly_copyable<I, O>
  constexpr ranges::copy_if_result<I, O>
    ranges::copy_if(I first, S last, O result, Pred pred, Proj proj = {});
template<input_range R, weakly_incrementable O, class Proj = identity,
         indirect_unary_predicate<projected<iterator_t<R>, Proj>> Pred>
  requires indirectly_copyable<iterator_t<R>, O>
  constexpr ranges::copy_if_result<borrowed_iterator_t<R>, O>
    ranges::copy_if(R&& r, O result, Pred pred, Proj proj = {});
```

**15** Let *E* be:

- `bool(pred(*i))` for the overloads in namespace `std`;
- `bool(invoke(pred, invoke(proj, *i)))` for the overloads in namespace `ranges`,

and *N* be the number of iterators `i` in the range `[first, last)` for which the condition *E* holds.

**16** *Preconditions:* The ranges `[first, last)` and `[result, result + (last - first))` do not overlap. [*Note 1*: For the overload with an `ExecutionPolicy`, there may be a performance cost if `iterator_traits<ForwardIterator1>::value_type` is not *Cpp17MoveConstructible* ([Table 28](../library/utility.arg.requirements.md#tab:cpp17.moveconstructible)). — *end note*]

**17** *Effects:* Copies all of the elements referred to by the iterator `i` in the range `[first, last)` for which *E* is `true`.

**18** *Returns:*

- `result + N` for the overloads in namespace `std`.
- `{last, result + N}` for the overloads in namespace `ranges`.

**19** *Complexity:* Exactly `last - first` applications of the corresponding predicate and any projection.

**20** *Remarks:* Stable ([[algorithm.stable]](../library/algorithm.stable.md)).

```cpp
template<class BidirectionalIterator1, class BidirectionalIterator2>
  constexpr BidirectionalIterator2
    copy_backward(BidirectionalIterator1 first,
                  BidirectionalIterator1 last,
                  BidirectionalIterator2 result);

template<bidirectional_iterator I1, sentinel_for<I1> S1, bidirectional_iterator I2>
  requires indirectly_copyable<I1, I2>
  constexpr ranges::copy_backward_result<I1, I2>
    ranges::copy_backward(I1 first, S1 last, I2 result);
template<bidirectional_range R, bidirectional_iterator I>
  requires indirectly_copyable<iterator_t<R>, I>
  constexpr ranges::copy_backward_result<borrowed_iterator_t<R>, I>
    ranges::copy_backward(R&& r, I result);
```

**21** Let *N* be `last - first`.

**22** *Preconditions:* `result` is not in the range `(first, last]`.

**23** *Effects:* Copies elements in the range `[first, last)` into the range `[result - N, result)` starting from `last - 1` and proceeding to `first`.[^1] For each positive integer *n* ≤ *N*, performs `*(result - n) = *(last - n)`.

**24** *Returns:*

- `result - N` for the overload in namespace `std`.
- `{last, result - N}` for the overloads in namespace `ranges`.

**25** *Complexity:* Exactly *N* assignments.

[^1]: `copy_backward` should be used instead of copy when `last` is in the range `[result - N, result)`.
