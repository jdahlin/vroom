# 25.10.11 Adjacent difference [adjacent.difference]

[Contents](../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.10 Transform inclusive scan \[transform.inclusive.scan\]](transform.inclusive.scan.md) · next: [25.10.12 Iota \[numeric.iota\]](numeric.iota.md)

```cpp
template<class InputIterator, class OutputIterator>
  constexpr OutputIterator
    adjacent_difference(InputIterator first, InputIterator last,
                        OutputIterator result);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2>
  ForwardIterator2
    adjacent_difference(ExecutionPolicy&& exec,
                        ForwardIterator1 first, ForwardIterator1 last, ForwardIterator2 result);

template<class InputIterator, class OutputIterator, class BinaryOperation>
  constexpr OutputIterator
    adjacent_difference(InputIterator first, InputIterator last,
                        OutputIterator result, BinaryOperation binary_op);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
         class BinaryOperation>
  ForwardIterator2
    adjacent_difference(ExecutionPolicy&& exec,
                        ForwardIterator1 first, ForwardIterator1 last,
                        ForwardIterator2 result, BinaryOperation binary_op);
```

**1** Let `T` be the value type of `decltype(first)`. For the overloads that do not take an argument `binary_op`, let `binary_op` be an lvalue that denotes an object of type `minus<>`.

**2** *Mandates:*

- For the overloads with no `ExecutionPolicy`, `T` is constructible from `*first`. `acc` (defined below) is writable ([[iterator.requirements.general]](iterator.requirements.general.md)) to the `result` output iterator. The result of the expression `binary_op(val, std::move(acc))` is writable to `result`.
- For the overloads with an `ExecutionPolicy`, the result of the expressions `binary_op(*first, *first)` and `*first` are writable to `result`.

**3** *Preconditions:*

- For the overloads with no `ExecutionPolicy`, `T` meets the *Cpp17MoveAssignable* ([Table 30](utility.arg.requirements.md#tab:cpp17.moveassignable)) requirements.
- For all overloads, in the ranges `[first, last]` and `[result, result + (last - first)]`, `binary_op` neither modifies elements nor invalidate iterators or subranges.[^1]

**4** *Effects:* For the overloads with no `ExecutionPolicy` and a non-empty range, the function creates an accumulator `acc` of type `T`, initializes it with `*first`, and assigns the result to `*result`. For every iterator `i` in `[first + 1, last)` in order, creates an object `val` whose type is `T`, initializes it with `*i`, computes `binary_op(val, std::move(acc))`, assigns the result to `*(result + (i - first))`, and move assigns from `val` to `acc`.

**5** For the overloads with an `ExecutionPolicy` and a non-empty range, performs `*result = *first`. Then, for every `d` in `[1, last - first - 1]`, performs `*(result + d) = binary_op(*(first + d), *(first + (d - 1)))`.

**6** *Returns:* `result + (last - first)`.

**7** *Complexity:* Exactly `(last - first) - 1` applications of the binary operation.

**8** *Remarks:* For the overloads with no `ExecutionPolicy`, `result` may be equal to `first`. For the overloads with an `ExecutionPolicy`, the ranges `[first, last)` and `[result, result + (last - first))` shall not overlap.

[^1]: The use of fully closed ranges is intentional.
