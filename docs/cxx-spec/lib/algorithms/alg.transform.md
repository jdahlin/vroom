# 25.7.4 Transform [alg.transform]

[Contents](../../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7.3 Swap \[alg.swap\]](alg.swap.md) · next: [25.7.5 Replace \[alg.replace\]](alg.replace.md)

```cpp
template<class InputIterator, class OutputIterator,
         class UnaryOperation>
  constexpr OutputIterator
    transform(InputIterator first1, InputIterator last1,
              OutputIterator result, UnaryOperation op);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
         class UnaryOperation>
  ForwardIterator2
    transform(ExecutionPolicy&& exec,
              ForwardIterator1 first1, ForwardIterator1 last1,
              ForwardIterator2 result, UnaryOperation op);

template<class InputIterator1, class InputIterator2,
         class OutputIterator, class BinaryOperation>
  constexpr OutputIterator
    transform(InputIterator1 first1, InputIterator1 last1,
              InputIterator2 first2, OutputIterator result,
              BinaryOperation binary_op);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
         class ForwardIterator, class BinaryOperation>
  ForwardIterator
    transform(ExecutionPolicy&& exec,
              ForwardIterator1 first1, ForwardIterator1 last1,
              ForwardIterator2 first2, ForwardIterator result,
              BinaryOperation binary_op);

template<input_iterator I, sentinel_for<I> S, weakly_incrementable O,
         copy_constructible F, class Proj = identity>
  requires indirectly_writable<O, indirect_result_t<F&, projected<I, Proj>>>
  constexpr ranges::unary_transform_result<I, O>
    ranges::transform(I first1, S last1, O result, F op, Proj proj = {});
template<input_range R, weakly_incrementable O, copy_constructible F,
         class Proj = identity>
  requires indirectly_writable<O, indirect_result_t<F&, projected<iterator_t<R>, Proj>>>
  constexpr ranges::unary_transform_result<borrowed_iterator_t<R>, O>
    ranges::transform(R&& r, O result, F op, Proj proj = {});
template<input_iterator I1, sentinel_for<I1> S1, input_iterator I2, sentinel_for<I2> S2,
         weakly_incrementable O, copy_constructible F, class Proj1 = identity,
         class Proj2 = identity>
  requires indirectly_writable<O, indirect_result_t<F&, projected<I1, Proj1>,
                                         projected<I2, Proj2>>>
  constexpr ranges::binary_transform_result<I1, I2, O>
    ranges::transform(I1 first1, S1 last1, I2 first2, S2 last2, O result,
                      F binary_op, Proj1 proj1 = {}, Proj2 proj2 = {});
template<input_range R1, input_range R2, weakly_incrementable O,
         copy_constructible F, class Proj1 = identity, class Proj2 = identity>
  requires indirectly_writable<O, indirect_result_t<F&, projected<iterator_t<R1>, Proj1>,
                                         projected<iterator_t<R2>, Proj2>>>
  constexpr ranges::binary_transform_result<borrowed_iterator_t<R1>, borrowed_iterator_t<R2>, O>
    ranges::transform(R1&& r1, R2&& r2, O result,
                      F binary_op, Proj1 proj1 = {}, Proj2 proj2 = {});
```

**1** Let:

- `last2` be `first2 + (last1 - first1)` for the overloads with parameter `first2` but no parameter `last2`;

- *N* be `last1 - first1` for unary transforms, or min(`last1 - first1`, `last2 - first2`) for binary transforms;

- *E* be

  - `op(*(first1 + (i - result)))` for unary transforms defined in namespace `std`;
  - `binary_op(*(first1 + (i - result)), *(first2 + (i - result)))` for binary transforms defined in namespace `std`;
  - `invoke(op, invoke(proj, *(first1 + (i - result))))` for unary transforms defined in namespace `ranges`;
  - `invoke(binary_op, invoke(proj1, *(first1 + (i - result))), invoke(proj2,*(first2 + (i - result))))` for binary transforms defined in namespace `ranges`.

**2** *Preconditions:* `op` and `binary_op` do not invalidate iterators or subranges, nor modify elements in the ranges

- `[first1, first1 + N]`,
- `[first2, first2 + N]`, and
- `[result, result + N]`.[^1]

**3** *Effects:* Assigns through every iterator `i` in the range `[result, result + N)` a new corresponding value equal to *E*.

**4** *Returns:*

- `result + N` for the overloads defined in namespace `std`.
- `{first1 + N, result + N}` for unary transforms defined in namespace `ranges`.
- `{first1 + N, first2 + N, result + N}` for binary transforms defined in namespace `ranges`.

**5** *Complexity:* Exactly *N* applications of `op` or `binary_op`, and any projections. This requirement also applies to the overload with an `ExecutionPolicy`.

**6** *Remarks:* `result` may be equal to `first1` or `first2`.

[^1]: The use of fully closed ranges is intentional.
