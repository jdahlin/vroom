# 25.10.5 Transform reduce [transform.reduce]

[Contents](../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.4 Inner product \[inner.product\]](inner.product.md) · next: [25.10.6 Partial sum \[partial.sum\]](partial.sum.md)

```cpp
template<class InputIterator1, class InputIterator2, class T>
  constexpr T transform_reduce(InputIterator1 first1, InputIterator1 last1,
                               InputIterator2 first2,
                               T init);
```

**1** *Effects:* Equivalent to:

```cpp
return transform_reduce(first1, last1, first2, init, plus<>(), multiplies<>());
```

```cpp
template<class ExecutionPolicy,
         class ForwardIterator1, class ForwardIterator2, class T>
  T transform_reduce(ExecutionPolicy&& exec,
                     ForwardIterator1 first1, ForwardIterator1 last1,
                     ForwardIterator2 first2,
                     T init);
```

**2** *Effects:* Equivalent to:

```cpp
return transform_reduce(std::forward<ExecutionPolicy>(exec),
                        first1, last1, first2, init, plus<>(), multiplies<>());
```

```cpp
template<class InputIterator1, class InputIterator2, class T,
         class BinaryOperation1, class BinaryOperation2>
  constexpr T transform_reduce(InputIterator1 first1, InputIterator1 last1,
                               InputIterator2 first2,
                               T init,
                               BinaryOperation1 binary_op1,
                               BinaryOperation2 binary_op2);
template<class ExecutionPolicy,
         class ForwardIterator1, class ForwardIterator2, class T,
         class BinaryOperation1, class BinaryOperation2>
  T transform_reduce(ExecutionPolicy&& exec,
                     ForwardIterator1 first1, ForwardIterator1 last1,
                     ForwardIterator2 first2,
                     T init,
                     BinaryOperation1 binary_op1,
                     BinaryOperation2 binary_op2);
```

**3** *Mandates:* All of

- `binary_op1(init, init)`,
- `binary_op1(init, binary_op2(*first1, *first2))`,
- `binary_op1(binary_op2(*first1, *first2), init)`, and
- `binary_op1(binary_op2(*first1, *first2), binary_op2(*first1, *first2))`

are convertible to `T`.

**4** *Preconditions:*

- `T` meets the *Cpp17MoveConstructible* ([Table 28](utility.arg.requirements.md#tab:cpp17.moveconstructible)) requirements.
- Neither `binary_op1` nor `binary_op2` invalidates subranges, nor modifies elements in the ranges `[first1, last1]` and `[first2, first2 + (last1 - first1)]`.

**5** *Returns:*

```cpp
GENERALIZED_SUM(binary_op1, init, binary_op2(*i, *(first2 + (i - first1))), ...)
```

for every iterator `i` in `[first1, last1)`.

**6** *Complexity:* 𝑂(`last1 - first1`) applications each of `binary_op1` and `binary_op2`.

```cpp
template<class InputIterator, class T,
         class BinaryOperation, class UnaryOperation>
  constexpr T transform_reduce(InputIterator first, InputIterator last, T init,
                               BinaryOperation binary_op, UnaryOperation unary_op);
template<class ExecutionPolicy,
         class ForwardIterator, class T,
         class BinaryOperation, class UnaryOperation>
  T transform_reduce(ExecutionPolicy&& exec,
                     ForwardIterator first, ForwardIterator last,
                     T init, BinaryOperation binary_op, UnaryOperation unary_op);
```

**7** *Mandates:* All of

- `binary_op(init, init)`,
- `binary_op(init, unary_op(*first))`,
- `binary_op(unary_op(*first), init)`, and
- `binary_op(unary_op(*first), unary_op(*first))`

are convertible to `T`.

**8** *Preconditions:*

- `T` meets the *Cpp17MoveConstructible* ([Table 28](utility.arg.requirements.md#tab:cpp17.moveconstructible)) requirements.
- Neither `unary_op` nor `binary_op` invalidates subranges, nor modifies elements in the range `[first, last]`.

**9** *Returns:*

```cpp
GENERALIZED_SUM(binary_op, init, unary_op(*i), ...)
```

for every iterator `i` in `[first, last)`.

**10** *Complexity:* 𝑂(`last - first`) applications each of `unary_op` and `binary_op`.

**11** [*Note 1*: `transform_reduce` does not apply `unary_op` to `init`. — *end note*]
