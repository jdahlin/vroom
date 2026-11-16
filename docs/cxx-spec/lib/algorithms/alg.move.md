# 25.7.2 Move [alg.move]

[Contents](../../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7.1 Copy \[alg.copy\]](alg.copy.md) · next: [25.7.3 Swap \[alg.swap\]](alg.swap.md)

```cpp
template<class InputIterator, class OutputIterator>
  constexpr OutputIterator move(InputIterator first, InputIterator last,
                                OutputIterator result);

template<input_iterator I, sentinel_for<I> S, weakly_incrementable O>
  requires indirectly_movable<I, O>
  constexpr ranges::move_result<I, O>
    ranges::move(I first, S last, O result);
template<input_range R, weakly_incrementable O>
  requires indirectly_movable<iterator_t<R>, O>
  constexpr ranges::move_result<borrowed_iterator_t<R>, O>
    ranges::move(R&& r, O result);
```

**1** Let *E* be

- `std::move(*(first + n))` for the overload in namespace `std`;
- `ranges::iter_move(first + n)` for the overloads in namespace `ranges`.

Let *N* be `last - first`.

**2** *Preconditions:* `result` is not in the range `[first, last)`.

**3** *Effects:* Moves elements in the range `[first, last)` into the range `[result, result + N)` starting from `first` and proceeding to `last`. For each non-negative integer *n* &lt; *N*, performs `*(result + n) = E`.

**4** *Returns:*

- `result + N` for the overload in namespace `std`.
- `{last, result + N}` for the overloads in namespace `ranges`.

**5** *Complexity:* Exactly *N* assignments.

```cpp
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2>
  ForwardIterator2 move(ExecutionPolicy&& policy,
                        ForwardIterator1 first, ForwardIterator1 last,
                        ForwardIterator2 result);
```

**6** Let *N* be `last - first`.

**7** *Preconditions:* The ranges `[first, last)` and `[result, result + N)` do not overlap.

**8** *Effects:* Moves elements in the range `[first, last)` into the range `[result, result + N)`. For each non-negative integer *n* &lt; *N*, performs `*(result + n) = std::move(*(first + n))`.

**9** *Returns:* `result + N`.

**10** *Complexity:* Exactly *N* assignments.

```cpp
template<class BidirectionalIterator1, class BidirectionalIterator2>
  constexpr BidirectionalIterator2
    move_backward(BidirectionalIterator1 first, BidirectionalIterator1 last,
                  BidirectionalIterator2 result);

template<bidirectional_iterator I1, sentinel_for<I1> S1, bidirectional_iterator I2>
  requires indirectly_movable<I1, I2>
  constexpr ranges::move_backward_result<I1, I2>
    ranges::move_backward(I1 first, S1 last, I2 result);
template<bidirectional_range R, bidirectional_iterator I>
  requires indirectly_movable<iterator_t<R>, I>
  constexpr ranges::move_backward_result<borrowed_iterator_t<R>, I>
    ranges::move_backward(R&& r, I result);
```

**11** Let *E* be

- `std::move(*(last - n))` for the overload in namespace `std`;
- `ranges::iter_move(last - n)` for the overloads in namespace `ranges`.

Let *N* be `last - first`.

**12** *Preconditions:* `result` is not in the range `(first, last]`.

**13** *Effects:* Moves elements in the range `[first, last)` into the range `[result - N, result)` starting from `last - 1` and proceeding to `first`.[^1] For each positive integer *n* ≤ *N*, performs `*(result - n) = E`.

**14** *Returns:*

- `result - N` for the overload in namespace `std`.
- `{last, result - N}` for the overloads in namespace `ranges`.

**15** *Complexity:* Exactly *N* assignments.

[^1]: `move_backward` should be used instead of move when `last` is in the range `[result - N, result)`.
