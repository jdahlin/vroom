# 25.10.10 Transform inclusive scan [transform.inclusive.scan]

[Contents](../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.9 Transform exclusive scan \[transform.exclusive.scan\]](transform.exclusive.scan.md) · next: [25.10.11 Adjacent difference \[adjacent.difference\]](adjacent.difference.md)

```cpp
template<class InputIterator, class OutputIterator,
         class BinaryOperation, class UnaryOperation>
  constexpr OutputIterator
    transform_inclusive_scan(InputIterator first, InputIterator last,
                             OutputIterator result,
                             BinaryOperation binary_op, UnaryOperation unary_op);
template<class ExecutionPolicy,
         class ForwardIterator1, class ForwardIterator2,
         class BinaryOperation, class UnaryOperation>
  ForwardIterator2
    transform_inclusive_scan(ExecutionPolicy&& exec,
                             ForwardIterator1 first, ForwardIterator1 last,
                             ForwardIterator2 result,
                             BinaryOperation binary_op, UnaryOperation unary_op);
template<class InputIterator, class OutputIterator,
         class BinaryOperation, class UnaryOperation, class T>
  constexpr OutputIterator
    transform_inclusive_scan(InputIterator first, InputIterator last,
                             OutputIterator result,
                             BinaryOperation binary_op, UnaryOperation unary_op,
                             T init);
template<class ExecutionPolicy,
         class ForwardIterator1, class ForwardIterator2,
         class BinaryOperation, class UnaryOperation, class T>
  ForwardIterator2
    transform_inclusive_scan(ExecutionPolicy&& exec,
                             ForwardIterator1 first, ForwardIterator1 last,
                             ForwardIterator2 result,
                             BinaryOperation binary_op, UnaryOperation unary_op,
                             T init);
```

**1** Let `U` be the value type of `decltype(first)`.

**2** *Mandates:* If `init` is provided, all of

- `binary_op(init, init)`,
- `binary_op(init, unary_op(*first))`, and
- `binary_op(unary_op(*first), unary_op(*first))`

are convertible to `T`; otherwise, `binary_op(unary_op(*first), unary_op(*first))` is convertible to `U`.

**3** *Preconditions:*

- If `init` is provided, `T` meets the *Cpp17MoveConstructible* ([Table 28](utility.arg.requirements.md#tab:cpp17.moveconstructible)) requirements; otherwise, `U` meets the *Cpp17MoveConstructible* requirements.
- Neither `unary_op` nor `binary_op` invalidates iterators or subranges, nor modifies elements in the ranges `[first, last]` or `[result, result + (last - first)]`.

**4** *Effects:* For each integer `K` in `[0, last - first)` assigns through `result + K` the value of

- `GENERALIZED_NONCOMMUTATIVE_SUM( binary_op, init, unary_op(*(first + 0)), unary_op(*(first + 1)), ..., unary_op(*(first + K)))`<br> if `init` is provided, or
- `GENERALIZED_NONCOMMUTATIVE_SUM( binary_op, unary_op(*(first + 0)), unary_op(*(first + 1)), ..., unary_op(*(first + K)))`<br> otherwise.

**5** *Returns:* The end of the resulting range beginning at `result`.

**6** *Complexity:* 𝑂(`last - first`) applications each of `unary_op` and `binary_op`.

**7** *Remarks:* `result` may be equal to `first`.

**8** [*Note 1*: The difference between `transform_exclusive_scan` and `transform_inclusive_scan` is that `transform_inclusive_scan` includes the *i*<sup></sup>*th* input element in the *i*<sup></sup>*th* sum. If `binary_op` is not mathematically associative, the behavior of `transform_inclusive_scan` may be nondeterministic. `transform_inclusive_scan` does not apply `unary_op` to `init`. — *end note*]
