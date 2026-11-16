# 25.7.3 Swap [alg.swap]

[Contents](../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7.2 Move \[alg.move\]](alg.move.md) · next: [25.7.4 Transform \[alg.transform\]](alg.transform.md)

```cpp
template<class ForwardIterator1, class ForwardIterator2>
  constexpr ForwardIterator2
    swap_ranges(ForwardIterator1 first1, ForwardIterator1 last1,
                ForwardIterator2 first2);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2>
  ForwardIterator2
    swap_ranges(ExecutionPolicy&& exec,
                ForwardIterator1 first1, ForwardIterator1 last1,
                ForwardIterator2 first2);

template<input_iterator I1, sentinel_for<I1> S1, input_iterator I2, sentinel_for<I2> S2>
  requires indirectly_swappable<I1, I2>
  constexpr ranges::swap_ranges_result<I1, I2>
    ranges::swap_ranges(I1 first1, S1 last1, I2 first2, S2 last2);
template<input_range R1, input_range R2>
  requires indirectly_swappable<iterator_t<R1>, iterator_t<R2>>
  constexpr ranges::swap_ranges_result<borrowed_iterator_t<R1>, borrowed_iterator_t<R2>>
    ranges::swap_ranges(R1&& r1, R2&& r2);
```

**1** Let:

- `last2` be `first2 + (last1 - first1)` for the overloads with no parameter named `last2`;
- *M* be min(`last1 - first1`, `last2 - first2`).

**2** *Preconditions:* The two ranges `[first1, last1)` and `[first2, last2)` do not overlap. For the overloads in namespace `std`, `*(first1 + n)` is swappable with ([[swappable.requirements]](swappable.requirements.md)) `*(first2 + n)`.

**3** *Effects:* For each non-negative integer *n* &lt; *M* performs:

- `swap(*(first1 + n), *(first2 + n))` for the overloads in namespace `std`;
- `ranges::iter_swap(first1 + n, first2 + n)` for the overloads in namespace `ranges`.

**4** *Returns:*

- `last2` for the overloads in namespace `std`.
- `{first1 + M, first2 + M}` for the overloads in namespace `ranges`.

**5** *Complexity:* Exactly *M* swaps.

```cpp
template<class ForwardIterator1, class ForwardIterator2>
  constexpr void iter_swap(ForwardIterator1 a, ForwardIterator2 b);
```

**6** *Preconditions:* `a` and `b` are dereferenceable. `*a` is swappable with ([[swappable.requirements]](swappable.requirements.md)) `*b`.

**7** *Effects:* As if by `swap(*a, *b)`.
