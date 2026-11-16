# 25.7.14 Shift [alg.shift]

[Contents](../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7.13 Shuffle \[alg.random.shuffle\]](alg.random.shuffle.md) · next: [25.8 Sorting and related operations \[alg.sorting\]](alg.sorting.md)

```cpp
template<class ForwardIterator>
  constexpr ForwardIterator
    shift_left(ForwardIterator first, ForwardIterator last,
               typename iterator_traits<ForwardIterator>::difference_type n);
template<class ExecutionPolicy, class ForwardIterator>
  ForwardIterator
    shift_left(ExecutionPolicy&& exec, ForwardIterator first, ForwardIterator last,
               typename iterator_traits<ForwardIterator>::difference_type n);
```

**1** *Preconditions:* `n >= 0` is `true`. The type of `*first` meets the *Cpp17MoveAssignable* requirements.

**2** *Effects:* If `n == 0` or `n >= last - first`, does nothing. Otherwise, moves the element from position `first + n + i` into position `first + i` for each non-negative integer `i < (last - first) - n`. In the first overload case, does so in order starting from `i = 0` and proceeding to `i = (last - first) - n - 1`.

**3** *Returns:* `first + (last - first - n)` if `n < last - first`, otherwise `first`.

**4** *Complexity:* At most `(last - first) - n` assignments.

```cpp
template<class ForwardIterator>
  constexpr ForwardIterator
    shift_right(ForwardIterator first, ForwardIterator last,
                typename iterator_traits<ForwardIterator>::difference_type n);
template<class ExecutionPolicy, class ForwardIterator>
  ForwardIterator
    shift_right(ExecutionPolicy&& exec, ForwardIterator first, ForwardIterator last,
                typename iterator_traits<ForwardIterator>::difference_type n);
```

**5** *Preconditions:* `n >= 0` is `true`. The type of `*first` meets the *Cpp17MoveAssignable* requirements. `ForwardIterator` meets the *Cpp17BidirectionalIterator* requirements ([[bidirectional.iterators]](bidirectional.iterators.md)) or the *Cpp17ValueSwappable* requirements.

**6** *Effects:* If `n == 0` or `n >= last - first`, does nothing. Otherwise, moves the element from position `first + i` into position `first + n + i` for each non-negative integer `i < (last - first) - n`. In the first overload case, if `ForwardIterator` meets the *Cpp17BidirectionalIterator* requirements, does so in order starting from `i = (last - first) - n - 1` and proceeding to `i = 0`.

**7** *Returns:* `first + n` if `n < last - first`, otherwise `last`.

**8** *Complexity:* At most `(last - first) - n` assignments or swaps.
