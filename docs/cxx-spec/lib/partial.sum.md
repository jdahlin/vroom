# 25.10.6 Partial sum [partial.sum]

[Contents](../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.5 Transform reduce \[transform.reduce\]](transform.reduce.md) · next: [25.10.7 Exclusive scan \[exclusive.scan\]](exclusive.scan.md)

```cpp
template<class InputIterator, class OutputIterator>
  constexpr OutputIterator
    partial_sum(InputIterator first, InputIterator last,
                OutputIterator result);
template<class InputIterator, class OutputIterator, class BinaryOperation>
  constexpr OutputIterator
    partial_sum(InputIterator first, InputIterator last,
                OutputIterator result, BinaryOperation binary_op);
```

**1** *Mandates:* `InputIterator`’s value type is constructible from `*first`. The result of the expression `std::move(acc) + *i` or `binary_op(std::move(acc), *i)` is implicitly convertible to `InputIterator`’s value type. `acc` is writable ([[iterator.requirements.general]](iterator.requirements.general.md)) to `result`.

**2** *Preconditions:* In the ranges `[first, last]` and `[result, result + (last - first)]` `binary_op` neither modifies elements nor invalidates iterators or subranges.[^1]

**3** *Effects:* For a non-empty range, the function creates an accumulator `acc` whose type is `InputIterator`’s value type, initializes it with `*first`, and assigns the result to `*result`. For every iterator `i` in `[first + 1, last)` in order, `acc` is then modified by `acc = std::move(acc) + *i` or `acc = binary_op(std::move(acc), *i)` and the result is assigned to `*(result + (i - first))`.

**4** *Returns:* `result + (last - first)`.

**5** *Complexity:* Exactly `(last - first) - 1` applications of the binary operation.

**6** *Remarks:* `result` may be equal to `first`.

[^1]: The use of fully closed ranges is intentional.
