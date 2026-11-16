# 25.10.8 Inclusive scan [inclusive.scan]

[Contents](../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.7 Exclusive scan \[exclusive.scan\]](exclusive.scan.md) · next: [25.10.9 Transform exclusive scan \[transform.exclusive.scan\]](transform.exclusive.scan.md)

```cpp
template<class InputIterator, class OutputIterator>
  constexpr OutputIterator
    inclusive_scan(InputIterator first, InputIterator last,
                   OutputIterator result);
```

**1** *Effects:* Equivalent to:

```cpp
return inclusive_scan(first, last, result, plus<>());
```

```cpp
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2>
  ForwardIterator2
    inclusive_scan(ExecutionPolicy&& exec,
                   ForwardIterator1 first, ForwardIterator1 last,
                   ForwardIterator2 result);
```

**2** *Effects:* Equivalent to:

```cpp
return inclusive_scan(std::forward<ExecutionPolicy>(exec), first, last, result, plus<>());
```

```cpp
template<class InputIterator, class OutputIterator, class BinaryOperation>
  constexpr OutputIterator
    inclusive_scan(InputIterator first, InputIterator last,
                   OutputIterator result, BinaryOperation binary_op);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
         class BinaryOperation>
  ForwardIterator2
    inclusive_scan(ExecutionPolicy&& exec,
                   ForwardIterator1 first, ForwardIterator1 last,
                   ForwardIterator2 result, BinaryOperation binary_op);

template<class InputIterator, class OutputIterator, class BinaryOperation, class T>
  constexpr OutputIterator
    inclusive_scan(InputIterator first, InputIterator last,
                   OutputIterator result, BinaryOperation binary_op, T init);
template<class ExecutionPolicy,
         class ForwardIterator1, class ForwardIterator2, class BinaryOperation, class T>
  ForwardIterator2
    inclusive_scan(ExecutionPolicy&& exec,
                   ForwardIterator1 first, ForwardIterator1 last,
                   ForwardIterator2 result, BinaryOperation binary_op, T init);
```

**3** Let `U` be the value type of `decltype(first)`.

**4** *Mandates:* If `init` is provided, all of

- `binary_op(init, init)`,
- `binary_op(init, *first)`, and
- `binary_op(*first, *first)`

are convertible to `T`; otherwise, `binary_op(*first, *first)` is convertible to `U`.

**5** *Preconditions:*

- If `init` is provided, `T` meets the *Cpp17MoveConstructible* ([Table 28](utility.arg.requirements.md#tab:cpp17.moveconstructible)) requirements; otherwise, `U` meets the *Cpp17MoveConstructible* requirements.
- `binary_op` neither invalidates iterators or subranges, nor modifies elements in the ranges `[first, last]` or `[result, result + (last - first)]`.

**6** *Effects:* For each integer `K` in `[0, last - first)` assigns through `result + K` the value of

- `GENERALIZED_NONCOMMUTATIVE_SUM( binary_op, init, *(first + 0), *(first + 1), ..., *(first + K))`<br>if `init` is provided, or
- `GENERALIZED_NONCOMMUTATIVE_SUM( binary_op, *(first + 0), *(first + 1), ..., *(first + K))`<br>otherwise.

**7** *Returns:* The end of the resulting range beginning at `result`.

**8** *Complexity:* 𝑂(`last - first`) applications of `binary_op`.

**9** *Remarks:* `result` may be equal to `first`.

**10** [*Note 1*: The difference between `exclusive_scan` and `inclusive_scan` is that `inclusive_scan` includes the *i*<sup></sup>*th* input element in the *i*<sup></sup>*th* sum. If `binary_op` is not mathematically associative, the behavior of `inclusive_scan` may be nondeterministic. — *end note*]
