# 25.7.6 Fill [alg.fill]

[Contents](../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7.5 Replace \[alg.replace\]](alg.replace.md) · next: [25.7.7 Generate \[alg.generate\]](alg.generate.md)

```cpp
template<class ForwardIterator, class T>
  constexpr void fill(ForwardIterator first, ForwardIterator last, const T& value);
template<class ExecutionPolicy, class ForwardIterator, class T>
  void fill(ExecutionPolicy&& exec,
            ForwardIterator first, ForwardIterator last, const T& value);

template<class OutputIterator, class Size, class T>
  constexpr OutputIterator fill_n(OutputIterator first, Size n, const T& value);
template<class ExecutionPolicy, class ForwardIterator, class Size, class T>
  ForwardIterator fill_n(ExecutionPolicy&& exec,
                         ForwardIterator first, Size n, const T& value);


template<class T, output_iterator<const T&> O, sentinel_for<O> S>
  constexpr O ranges::fill(O first, S last, const T& value);
template<class T, output_range<const T&> R>
  constexpr borrowed_iterator_t<R> ranges::fill(R&& r, const T& value);
template<class T, output_iterator<const T&> O>
  constexpr O ranges::fill_n(O first, iter_difference_t<O> n, const T& value);
```

**1** Let *N* be max(0, `n`) for the `fill_n` algorithms, and `last - first` for the `fill` algorithms.

**2** *Mandates:* The expression `value` is writable ([[iterator.requirements.general]](iterator.requirements.general.md)) to the output iterator. The type `Size` is convertible to an integral type ([[conv.integral]](../conv.integral.md), [[class.conv]](../class.conv.md)).

**3** *Effects:* Assigns `value` through all the iterators in the range `[first, first + N)`.

**4** *Returns:* `first + N`.

**5** *Complexity:* Exactly *N* assignments.
