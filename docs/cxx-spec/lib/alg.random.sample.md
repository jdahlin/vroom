# 25.7.12 Sample [alg.random.sample]

[Contents](../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7.11 Rotate \[alg.rotate\]](alg.rotate.md) · next: [25.7.13 Shuffle \[alg.random.shuffle\]](alg.random.shuffle.md)

```cpp
template<class PopulationIterator, class SampleIterator,
         class Distance, class UniformRandomBitGenerator>
  SampleIterator sample(PopulationIterator first, PopulationIterator last,
                        SampleIterator out, Distance n,
                        UniformRandomBitGenerator&& g);

template<input_iterator I, sentinel_for<I> S, weakly_incrementable O, class Gen>
  requires (forward_iterator<I> || random_access_iterator<O>) &&
           indirectly_copyable<I, O> &&
           uniform_random_bit_generator<remove_reference_t<Gen>>
  O ranges::sample(I first, S last, O out, iter_difference_t<I> n, Gen&& g);
template<input_range R, weakly_incrementable O, class Gen>
  requires (forward_range<R> || random_access_iterator<O>) &&
           indirectly_copyable<iterator_t<R>, O> &&
           uniform_random_bit_generator<remove_reference_t<Gen>>
  O ranges::sample(R&& r, O out, range_difference_t<R> n, Gen&& g);
```

**1** *Mandates:* For the overload in namespace `std`, `Distance` is an integer type and `*first` is writable ([[iterator.requirements.general]](iterator.requirements.general.md)) to `out`.

**2** *Preconditions:* `out` is not in the range `[first, last)`. For the overload in namespace `std`:

- `PopulationIterator` meets the *Cpp17InputIterator* requirements ([[input.iterators]](input.iterators.md)).
- `SampleIterator` meets the *Cpp17OutputIterator* requirements ([[output.iterators]](output.iterators.md)).
- `SampleIterator` meets the *Cpp17RandomAccessIterator* requirements ([[random.access.iterators]](random.access.iterators.md)) unless `PopulationIterator` meets the *Cpp17ForwardIterator* requirements ([[forward.iterators]](forward.iterators.md)).
- `remove_reference_t<UniformRandomBitGenerator>` meets the requirements of a uniform random bit generator type ([[rand.req.urng]](rand.req.urng.md)).

**3** *Effects:* Copies min(`last - first`, `n`) elements (the *sample*) from `[first, last)` (the *population*) to `out` such that each possible sample has equal probability of appearance. [*Note 1*: Algorithms that obtain such effects include *selection sampling* and *reservoir sampling*. — *end note*]

**4** *Returns:* The end of the resulting sample range.

**5** *Complexity:* 𝑂(`last - first`).

**6** *Remarks:*

- For the overload in namespace `std`, stable if and only if `PopulationIterator` meets the *Cpp17ForwardIterator* requirements. For the first overload in namespace `ranges`, stable if and only if `I` models `forward_iterator`.
- To the extent that the implementation of this function makes use of random numbers, the object `g` serves as the implementation’s source of randomness.
