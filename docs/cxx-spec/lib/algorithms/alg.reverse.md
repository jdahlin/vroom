# 25.7.10 Reverse [alg.reverse]

[Contents](../../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7.9 Unique \[alg.unique\]](alg.unique.md) · next: [25.7.11 Rotate \[alg.rotate\]](alg.rotate.md)

```cpp
template<class BidirectionalIterator>
  constexpr void reverse(BidirectionalIterator first, BidirectionalIterator last);
template<class ExecutionPolicy, class BidirectionalIterator>
  void reverse(ExecutionPolicy&& exec,
               BidirectionalIterator first, BidirectionalIterator last);

template<bidirectional_iterator I, sentinel_for<I> S>
  requires permutable<I>
  constexpr I ranges::reverse(I first, S last);
template<bidirectional_range R>
  requires permutable<iterator_t<R>>
  constexpr borrowed_iterator_t<R> ranges::reverse(R&& r);
```

**1** *Preconditions:* For the overloads in namespace `std`, `BidirectionalIterator` meets the *Cpp17ValueSwappable* requirements ([[swappable.requirements]](../library/swappable.requirements.md)).

**2** *Effects:* For each non-negative integer `i < (last - first) / 2`, applies `std::iter_swap`, or `ranges::iter_swap` for the overloads in namespace `ranges`, to all pairs of iterators `first + i, (last - i) - 1`.

**3** *Returns:* `last` for the overloads in namespace `ranges`.

**4** *Complexity:* Exactly `(last - first)/2` swaps.

```cpp
template<class BidirectionalIterator, class OutputIterator>
  constexpr OutputIterator
    reverse_copy(BidirectionalIterator first, BidirectionalIterator last,
                 OutputIterator result);
template<class ExecutionPolicy, class BidirectionalIterator, class ForwardIterator>
  ForwardIterator
    reverse_copy(ExecutionPolicy&& exec,
                 BidirectionalIterator first, BidirectionalIterator last,
                 ForwardIterator result);

template<bidirectional_iterator I, sentinel_for<I> S, weakly_incrementable O>
  requires indirectly_copyable<I, O>
  constexpr ranges::reverse_copy_result<I, O>
    ranges::reverse_copy(I first, S last, O result);
template<bidirectional_range R, weakly_incrementable O>
  requires indirectly_copyable<iterator_t<R>, O>
  constexpr ranges::reverse_copy_result<borrowed_iterator_t<R>, O>
    ranges::reverse_copy(R&& r, O result);
```

**5** Let *N* be `last - first`.

**6** *Preconditions:* The ranges `[first, last)` and `[result, result + N)` do not overlap.

**7** *Effects:* Copies the range `[first, last)` to the range `[result, result + N)` such that for every non-negative integer `i < N` the following assignment takes place: `*(result + N - 1 - i) = *(first + i)`.

**8** *Returns:*

- `result + N` for the overloads in namespace `std`.
- `{last, result + N}` for the overloads in namespace `ranges`.

**9** *Complexity:* Exactly *N* assignments.
