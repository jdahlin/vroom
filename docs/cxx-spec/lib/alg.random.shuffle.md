# 25.7.13 Shuffle [alg.random.shuffle]

[Contents](../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7.12 Sample \[alg.random.sample\]](alg.random.sample.md) · next: [25.7.14 Shift \[alg.shift\]](alg.shift.md)

```cpp
template<class RandomAccessIterator, class UniformRandomBitGenerator>
  void shuffle(RandomAccessIterator first,
               RandomAccessIterator last,
               UniformRandomBitGenerator&& g);

template<random_access_iterator I, sentinel_for<I> S, class Gen>
  requires permutable<I> &&
           uniform_random_bit_generator<remove_reference_t<Gen>>
  I ranges::shuffle(I first, S last, Gen&& g);
template<random_access_range R, class Gen>
  requires permutable<iterator_t<R>> &&
           uniform_random_bit_generator<remove_reference_t<Gen>>
  borrowed_iterator_t<R> ranges::shuffle(R&& r, Gen&& g);
```

**1** *Preconditions:* For the overload in namespace `std`:

- `RandomAccessIterator` meets the *Cpp17ValueSwappable* requirements ([[swappable.requirements]](swappable.requirements.md)).
- The type `remove_reference_t<UniformRandomBitGenerator>` meets the uniform random bit generator ([[rand.req.urng]](rand.req.urng.md)) requirements.

**2** *Effects:* Permutes the elements in the range `[first, last)` such that each possible permutation of those elements has equal probability of appearance.

**3** *Returns:* `last` for the overloads in namespace `ranges`.

**4** *Complexity:* Exactly `(last - first) - 1` swaps.

**5** *Remarks:* To the extent that the implementation of this function makes use of random numbers, the object referenced by `g` shall serve as the implementation’s source of randomness.
