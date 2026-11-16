# 25.7.7 Generate [alg.generate]

[Contents](../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7.6 Fill \[alg.fill\]](alg.fill.md) · next: [25.7.8 Remove \[alg.remove\]](alg.remove.md)

```cpp
template<class ForwardIterator, class Generator>
  constexpr void generate(ForwardIterator first, ForwardIterator last,
                          Generator gen);
template<class ExecutionPolicy, class ForwardIterator, class Generator>
  void generate(ExecutionPolicy&& exec,
                ForwardIterator first, ForwardIterator last,
                Generator gen);

template<class OutputIterator, class Size, class Generator>
  constexpr OutputIterator generate_n(OutputIterator first, Size n, Generator gen);
template<class ExecutionPolicy, class ForwardIterator, class Size, class Generator>
  ForwardIterator generate_n(ExecutionPolicy&& exec,
                             ForwardIterator first, Size n, Generator gen);

template<input_or_output_iterator O, sentinel_for<O> S, copy_constructible F>
  requires invocable<F&> && indirectly_writable<O, invoke_result_t<F&>>
  constexpr O ranges::generate(O first, S last, F gen);
template<class R, copy_constructible F>
  requires invocable<F&> && output_range<R, invoke_result_t<F&>>
  constexpr borrowed_iterator_t<R> ranges::generate(R&& r, F gen);
template<input_or_output_iterator O, copy_constructible F>
  requires invocable<F&> && indirectly_writable<O, invoke_result_t<F&>>
  constexpr O ranges::generate_n(O first, iter_difference_t<O> n, F gen);
```

**1** Let *N* be max(0, `n`) for the `generate_n` algorithms, and `last - first` for the `generate` algorithms.

**2** *Mandates:* `Size` is convertible to an integral type ([[conv.integral]](../conv.integral.md), [[class.conv]](../class.conv.md)).

**3** *Effects:* Assigns the result of successive evaluations of `gen()` through each iterator in the range `[first, first + N)`.

**4** *Returns:* `first + N`.

**5** *Complexity:* Exactly *N* evaluations of `gen()` and assignments.
