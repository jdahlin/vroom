# 25.10.7 Exclusive scan [exclusive.scan]

[Contents](../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.6 Partial sum \[partial.sum\]](partial.sum.md) · next: [25.10.8 Inclusive scan \[inclusive.scan\]](inclusive.scan.md)

```cpp
template<class InputIterator, class OutputIterator, class T>
  constexpr OutputIterator
    exclusive_scan(InputIterator first, InputIterator last,
                   OutputIterator result, T init);
```

**1** *Effects:* Equivalent to:

```cpp
return exclusive_scan(first, last, result, init, plus<>());
```

```cpp
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2, class T>
  ForwardIterator2
    exclusive_scan(ExecutionPolicy&& exec,
                   ForwardIterator1 first, ForwardIterator1 last,
                   ForwardIterator2 result, T init);
```

**2** *Effects:* Equivalent to:

```cpp
return exclusive_scan(std::forward<ExecutionPolicy>(exec),
                      first, last, result, init, plus<>());
```

```cpp
template<class InputIterator, class OutputIterator, class T, class BinaryOperation>
  constexpr OutputIterator
    exclusive_scan(InputIterator first, InputIterator last,
                   OutputIterator result, T init, BinaryOperation binary_op);
template<class ExecutionPolicy,
         class ForwardIterator1, class ForwardIterator2, class T, class BinaryOperation>
  ForwardIterator2
    exclusive_scan(ExecutionPolicy&& exec,
                   ForwardIterator1 first, ForwardIterator1 last,
                   ForwardIterator2 result, T init, BinaryOperation binary_op);
```

**3** *Mandates:* All of

- `binary_op(init, init)`,
- `binary_op(init, *first)`, and
- `binary_op(*first, *first)`

are convertible to `T`.

**4** *Preconditions:*

- `T` meets the *Cpp17MoveConstructible* ([Table 28](utility.arg.requirements.md#tab:cpp17.moveconstructible)) requirements.
- `binary_op` neither invalidates iterators or subranges, nor modifies elements in the ranges `[first, last]` or `[result, result + (last - first)]`.

**5** *Effects:* For each integer `K` in `[0, last - first)` assigns through `result + K` the value of:

```cpp
GENERALIZED_NONCOMMUTATIVE_SUM(
    binary_op, init, *(first + 0), *(first + 1), ..., *(first + K - 1))
```

**6** *Returns:* The end of the resulting range beginning at `result`.

**7** *Complexity:* 𝑂(`last - first`) applications of `binary_op`.

**8** *Remarks:* `result` may be equal to `first`.

**9** [*Note 1*: The difference between `exclusive_scan` and `inclusive_scan` is that `exclusive_scan` excludes the *i*<sup></sup>*th* input element from the *i*<sup></sup>*th* sum. If `binary_op` is not mathematically associative, the behavior of `exclusive_scan` may be nondeterministic. — *end note*]
