# 25.10.9 Transform exclusive scan [transform.exclusive.scan]

[Contents](../../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.8 Inclusive scan \[inclusive.scan\]](inclusive.scan.md) · next: [25.10.10 Transform inclusive scan \[transform.inclusive.scan\]](transform.inclusive.scan.md)

```cpp
template<class InputIterator, class OutputIterator, class T,
         class BinaryOperation, class UnaryOperation>
  constexpr OutputIterator
    transform_exclusive_scan(InputIterator first, InputIterator last,
                             OutputIterator result, T init,
                             BinaryOperation binary_op, UnaryOperation unary_op);
template<class ExecutionPolicy,
         class ForwardIterator1, class ForwardIterator2, class T,
         class BinaryOperation, class UnaryOperation>
  ForwardIterator2
    transform_exclusive_scan(ExecutionPolicy&& exec,
                             ForwardIterator1 first, ForwardIterator1 last,
                             ForwardIterator2 result, T init,
                             BinaryOperation binary_op, UnaryOperation unary_op);
```

**1** *Mandates:* All of

- `binary_op(init, init)`,
- `binary_op(init, unary_op(*first))`, and
- `binary_op(unary_op(*first), unary_op(*first))`

are convertible to `T`.

**2** *Preconditions:*

- `T` meets the *Cpp17MoveConstructible* ([Table 28](../library/utility.arg.requirements.md#tab:cpp17.moveconstructible)) requirements.
- Neither `unary_op` nor `binary_op` invalidates iterators or subranges, nor modifies elements in the ranges `[first, last]` or `[result, result + (last - first)]`.

**3** *Effects:* For each integer `K` in `[0, last - first)` assigns through `result + K` the value of:

```cpp
GENERALIZED_NONCOMMUTATIVE_SUM(
    binary_op, init,
    unary_op(*(first + 0)), unary_op(*(first + 1)), ..., unary_op(*(first + K - 1)))
```

**4** *Returns:* The end of the resulting range beginning at `result`.

**5** *Complexity:* 𝑂(`last - first`) applications each of `unary_op` and `binary_op`.

**6** *Remarks:* `result` may be equal to `first`.

**7** [*Note 1*: The difference between `transform_exclusive_scan` and `transform_inclusive_scan` is that `transform_exclusive_scan` excludes the *i*<sup></sup>*th* input element from the *i*<sup></sup>*th* sum. If `binary_op` is not mathematically associative, the behavior of `transform_exclusive_scan` may be nondeterministic. `transform_exclusive_scan` does not apply `unary_op` to `init`. — *end note*]
