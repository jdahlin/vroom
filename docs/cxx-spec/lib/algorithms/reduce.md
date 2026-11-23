# 25.10.3 Reduce [reduce]

[Contents](../../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10.2 Accumulate \[accumulate\]](accumulate.md) · next: [25.10.4 Inner product \[inner.product\]](inner.product.md)

```cpp
template<class InputIterator>
  constexpr typename iterator_traits<InputIterator>::value_type
    reduce(InputIterator first, InputIterator last);
```

**1** *Effects:* Equivalent to:

```cpp
return reduce(first, last,
              typename iterator_traits<InputIterator>::value_type{});
```

```cpp
template<class ExecutionPolicy, class ForwardIterator>
  typename iterator_traits<ForwardIterator>::value_type
    reduce(ExecutionPolicy&& exec,
           ForwardIterator first, ForwardIterator last);
```

**2** *Effects:* Equivalent to:

```cpp
return reduce(std::forward<ExecutionPolicy>(exec), first, last,
              typename iterator_traits<ForwardIterator>::value_type{});
```

```cpp
template<class InputIterator, class T>
  constexpr T reduce(InputIterator first, InputIterator last, T init);
```

**3** *Effects:* Equivalent to:

```cpp
return reduce(first, last, init, plus<>());
```

```cpp
template<class ExecutionPolicy, class ForwardIterator, class T>
  T reduce(ExecutionPolicy&& exec,
           ForwardIterator first, ForwardIterator last, T init);
```

**4** *Effects:* Equivalent to:

```cpp
return reduce(std::forward<ExecutionPolicy>(exec), first, last, init, plus<>());
```

```cpp
template<class InputIterator, class T, class BinaryOperation>
  constexpr T reduce(InputIterator first, InputIterator last, T init,
                     BinaryOperation binary_op);
template<class ExecutionPolicy, class ForwardIterator, class T, class BinaryOperation>
  T reduce(ExecutionPolicy&& exec,
           ForwardIterator first, ForwardIterator last, T init,
           BinaryOperation binary_op);
```

**5** *Mandates:* All of

- `binary_op(init, *first)`,
- `binary_op(*first, init)`,
- `binary_op(init, init)`, and
- `binary_op(*first, *first)`

are convertible to `T`.

**6** *Preconditions:*

- `T` meets the *Cpp17MoveConstructible* ([Table 28](../library/utility.arg.requirements.md#tab:cpp17.moveconstructible)) requirements.
- `binary_op` neither invalidates iterators or subranges, nor modifies elements in the range `[first, last]`.

**7** *Returns:* `GENERALIZED_SUM(binary_op, init, *i, ...)` for every `i` in `[first, last)`.

**8** *Complexity:* 𝑂(`last - first`) applications of `binary_op`.

**9** [*Note 1*: The difference between `reduce` and `accumulate` is that `reduce` applies `binary_op` in an unspecified order, which yields a nondeterministic result for non-associative or non-commutative `binary_op` such as floating-point addition. — *end note*]
