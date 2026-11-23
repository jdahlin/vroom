# 25.9 Header `<numeric>` synopsis [numeric.ops.overview]

[Contents](../../README.md) · up: [25 Algorithms library \[algorithms\]](algorithms.md) · prev: [25.8.12 Permutation generators \[alg.permutation.generators\]](alg.permutation.generators.md) · next: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md)

```cpp
namespace std {
  // [accumulate], accumulate
  template<class InputIterator, class T>
    constexpr T accumulate(InputIterator first, InputIterator last, T init);
  template<class InputIterator, class T, class BinaryOperation>
    constexpr T accumulate(InputIterator first, InputIterator last, T init,
                           BinaryOperation binary_op);

  // [reduce], reduce
  template<class InputIterator>
    constexpr typename iterator_traits<InputIterator>::value_type
      reduce(InputIterator first, InputIterator last);
  template<class InputIterator, class T>
    constexpr T reduce(InputIterator first, InputIterator last, T init);
  template<class InputIterator, class T, class BinaryOperation>
    constexpr T reduce(InputIterator first, InputIterator last, T init,
                       BinaryOperation binary_op);
  template<class ExecutionPolicy, class ForwardIterator>
    typename iterator_traits<ForwardIterator>::value_type
      reduce(ExecutionPolicy&& exec,                            // see [algorithms.parallel.overloads]
             ForwardIterator first, ForwardIterator last);
  template<class ExecutionPolicy, class ForwardIterator, class T>
    T reduce(ExecutionPolicy&& exec,                            // see [algorithms.parallel.overloads]
             ForwardIterator first, ForwardIterator last, T init);
  template<class ExecutionPolicy, class ForwardIterator, class T, class BinaryOperation>
    T reduce(ExecutionPolicy&& exec,                            // see [algorithms.parallel.overloads]
             ForwardIterator first, ForwardIterator last, T init, BinaryOperation binary_op);

  // [inner.product], inner product
  template<class InputIterator1, class InputIterator2, class T>
    constexpr T inner_product(InputIterator1 first1, InputIterator1 last1,
                              InputIterator2 first2, T init);
  template<class InputIterator1, class InputIterator2, class T,
           class BinaryOperation1, class BinaryOperation2>
    constexpr T inner_product(InputIterator1 first1, InputIterator1 last1,
                              InputIterator2 first2, T init,
                              BinaryOperation1 binary_op1, BinaryOperation2 binary_op2);

  // [transform.reduce], transform reduce
  template<class InputIterator1, class InputIterator2, class T>
    constexpr T transform_reduce(InputIterator1 first1, InputIterator1 last1,
                                 InputIterator2 first2, T init);
  template<class InputIterator1, class InputIterator2, class T,
           class BinaryOperation1, class BinaryOperation2>
    constexpr T transform_reduce(InputIterator1 first1, InputIterator1 last1,
                                 InputIterator2 first2, T init,
                                 BinaryOperation1 binary_op1, BinaryOperation2 binary_op2);
  template<class InputIterator, class T,
           class BinaryOperation, class UnaryOperation>
    constexpr T transform_reduce(InputIterator first, InputIterator last, T init,
                                 BinaryOperation binary_op, UnaryOperation unary_op);
  template<class ExecutionPolicy,
           class ForwardIterator1, class ForwardIterator2, class T>
    T transform_reduce(ExecutionPolicy&& exec,                  // see [algorithms.parallel.overloads]
                       ForwardIterator1 first1, ForwardIterator1 last1,
                       ForwardIterator2 first2, T init);
  template<class ExecutionPolicy,
           class ForwardIterator1, class ForwardIterator2, class T,
           class BinaryOperation1, class BinaryOperation2>
    T transform_reduce(ExecutionPolicy&& exec,                  // see [algorithms.parallel.overloads]
                       ForwardIterator1 first1, ForwardIterator1 last1,
                       ForwardIterator2 first2, T init,
                       BinaryOperation1 binary_op1, BinaryOperation2 binary_op2);
  template<class ExecutionPolicy, class ForwardIterator, class T,
           class BinaryOperation, class UnaryOperation>
    T transform_reduce(ExecutionPolicy&& exec,                  // see [algorithms.parallel.overloads]
                       ForwardIterator first, ForwardIterator last, T init,
                       BinaryOperation binary_op, UnaryOperation unary_op);

  // [partial.sum], partial sum
  template<class InputIterator, class OutputIterator>
    constexpr OutputIterator
      partial_sum(InputIterator first, InputIterator last,
                  OutputIterator result);
  template<class InputIterator, class OutputIterator, class BinaryOperation>
    constexpr OutputIterator
      partial_sum(InputIterator first, InputIterator last,
                  OutputIterator result, BinaryOperation binary_op);

  // [exclusive.scan], exclusive scan
  template<class InputIterator, class OutputIterator, class T>
    constexpr OutputIterator
      exclusive_scan(InputIterator first, InputIterator last,
                     OutputIterator result, T init);
  template<class InputIterator, class OutputIterator, class T, class BinaryOperation>
    constexpr OutputIterator
      exclusive_scan(InputIterator first, InputIterator last,
                     OutputIterator result, T init, BinaryOperation binary_op);
  template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2, class T>
    ForwardIterator2
      exclusive_scan(ExecutionPolicy&& exec,                    // see [algorithms.parallel.overloads]
                     ForwardIterator1 first, ForwardIterator1 last,
                     ForwardIterator2 result, T init);
  template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2, class T,
           class BinaryOperation>
    ForwardIterator2
      exclusive_scan(ExecutionPolicy&& exec,                    // see [algorithms.parallel.overloads]
                     ForwardIterator1 first, ForwardIterator1 last,
                     ForwardIterator2 result, T init, BinaryOperation binary_op);

  // [inclusive.scan], inclusive scan
  template<class InputIterator, class OutputIterator>
    constexpr OutputIterator
      inclusive_scan(InputIterator first, InputIterator last,
                     OutputIterator result);
  template<class InputIterator, class OutputIterator, class BinaryOperation>
    constexpr OutputIterator
      inclusive_scan(InputIterator first, InputIterator last,
                     OutputIterator result, BinaryOperation binary_op);
  template<class InputIterator, class OutputIterator, class BinaryOperation, class T>
    constexpr OutputIterator
      inclusive_scan(InputIterator first, InputIterator last,
                     OutputIterator result, BinaryOperation binary_op, T init);
  template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2>
    ForwardIterator2
      inclusive_scan(ExecutionPolicy&& exec,                    // see [algorithms.parallel.overloads]
                     ForwardIterator1 first, ForwardIterator1 last,
                     ForwardIterator2 result);
  template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
           class BinaryOperation>
    ForwardIterator2
      inclusive_scan(ExecutionPolicy&& exec,                    // see [algorithms.parallel.overloads]
                     ForwardIterator1 first, ForwardIterator1 last,
                     ForwardIterator2 result, BinaryOperation binary_op);
  template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
           class BinaryOperation, class T>
    ForwardIterator2
      inclusive_scan(ExecutionPolicy&& exec,                    // see [algorithms.parallel.overloads]
                     ForwardIterator1 first, ForwardIterator1 last,
                     ForwardIterator2 result, BinaryOperation binary_op, T init);

  // [transform.exclusive.scan], transform exclusive scan
  template<class InputIterator, class OutputIterator, class T,
           class BinaryOperation, class UnaryOperation>
    constexpr OutputIterator
      transform_exclusive_scan(InputIterator first, InputIterator last,
                               OutputIterator result, T init,
                               BinaryOperation binary_op, UnaryOperation unary_op);
  template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2, class T,
           class BinaryOperation, class UnaryOperation>
    ForwardIterator2
      transform_exclusive_scan(ExecutionPolicy&& exec,          // see [algorithms.parallel.overloads]
                               ForwardIterator1 first, ForwardIterator1 last,
                               ForwardIterator2 result, T init,
                               BinaryOperation binary_op, UnaryOperation unary_op);

  // [transform.inclusive.scan], transform inclusive scan
  template<class InputIterator, class OutputIterator,
           class BinaryOperation, class UnaryOperation>
    constexpr OutputIterator
      transform_inclusive_scan(InputIterator first, InputIterator last,
                               OutputIterator result,
                               BinaryOperation binary_op, UnaryOperation unary_op);
  template<class InputIterator, class OutputIterator,
           class BinaryOperation, class UnaryOperation, class T>
    constexpr OutputIterator
      transform_inclusive_scan(InputIterator first, InputIterator last,
                               OutputIterator result,
                               BinaryOperation binary_op, UnaryOperation unary_op, T init);
  template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
           class BinaryOperation, class UnaryOperation>
    ForwardIterator2
      transform_inclusive_scan(ExecutionPolicy&& exec,          // see [algorithms.parallel.overloads]
                               ForwardIterator1 first, ForwardIterator1 last,
                               ForwardIterator2 result, BinaryOperation binary_op,
                               UnaryOperation unary_op);
  template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
           class BinaryOperation, class UnaryOperation, class T>
    ForwardIterator2
      transform_inclusive_scan(ExecutionPolicy&& exec,          // see [algorithms.parallel.overloads]
                               ForwardIterator1 first, ForwardIterator1 last,
                               ForwardIterator2 result,
                               BinaryOperation binary_op, UnaryOperation unary_op, T init);

  // [adjacent.difference], adjacent difference
  template<class InputIterator, class OutputIterator>
    constexpr OutputIterator
      adjacent_difference(InputIterator first, InputIterator last,
                          OutputIterator result);
  template<class InputIterator, class OutputIterator, class BinaryOperation>
    constexpr OutputIterator
      adjacent_difference(InputIterator first, InputIterator last,
                          OutputIterator result, BinaryOperation binary_op);
  template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2>
    ForwardIterator2
      adjacent_difference(ExecutionPolicy&& exec,               // see [algorithms.parallel.overloads]
                          ForwardIterator1 first, ForwardIterator1 last,
                          ForwardIterator2 result);
  template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
           class BinaryOperation>
    ForwardIterator2
      adjacent_difference(ExecutionPolicy&& exec,               // see [algorithms.parallel.overloads]
                          ForwardIterator1 first, ForwardIterator1 last,
                          ForwardIterator2 result, BinaryOperation binary_op);

  // [numeric.iota], iota
  template<class ForwardIterator, class T>
    constexpr void iota(ForwardIterator first, ForwardIterator last, T value);

  // [numeric.ops.gcd], greatest common divisor
  template<class M, class N>
    constexpr common_type_t<M,N> gcd(M m, N n);

  // [numeric.ops.lcm], least common multiple
  template<class M, class N>
    constexpr common_type_t<M,N> lcm(M m, N n);

  // [numeric.ops.midpoint], midpoint
  template<class T>
    constexpr T midpoint(T a, T b) noexcept;
  template<class T>
    constexpr T* midpoint(T* a, T* b);
}
```
