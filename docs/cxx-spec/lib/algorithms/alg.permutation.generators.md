# 25.8.12 Permutation generators [alg.permutation.generators]

[Contents](../../README.md) · up: [25.8 Sorting and related operations \[alg.sorting\]](alg.sorting.md) · prev: [25.8.11 Three-way comparison algorithms \[alg.three.way\]](alg.three.way.md) · next: [25.9 Header `<numeric>` synopsis \[numeric.ops.overview\]](numeric.ops.overview.md)

```cpp
template<class BidirectionalIterator>
  constexpr bool next_permutation(BidirectionalIterator first,
                                  BidirectionalIterator last);

template<class BidirectionalIterator, class Compare>
  constexpr bool next_permutation(BidirectionalIterator first,
                                  BidirectionalIterator last, Compare comp);

template<bidirectional_iterator I, sentinel_for<I> S, class Comp = ranges::less,
         class Proj = identity>
  requires sortable<I, Comp, Proj>
  constexpr ranges::next_permutation_result<I>
    ranges::next_permutation(I first, S last, Comp comp = {}, Proj proj = {});
template<bidirectional_range R, class Comp = ranges::less,
         class Proj = identity>
  requires sortable<iterator_t<R>, Comp, Proj>
  constexpr ranges::next_permutation_result<borrowed_iterator_t<R>>
    ranges::next_permutation(R&& r, Comp comp = {}, Proj proj = {});
```

**1** Let `comp` be `less{}` and `proj` be `identity{}` for overloads with no parameters by those names.

**2** *Preconditions:* For the overloads in namespace `std`, `BidirectionalIterator` meets the *Cpp17ValueSwappable* requirements ([[swappable.requirements]](../library/swappable.requirements.md)).

**3** *Effects:* Takes a sequence defined by the range `[first, last)` and transforms it into the next permutation. The next permutation is found by assuming that the set of all permutations is lexicographically sorted with respect to `comp` and `proj`. If no such permutation exists, transforms the sequence into the first permutation; that is, the ascendingly-sorted one.

**4** *Returns:* Let `B` be `true` if a next permutation was found and otherwise `false`. Returns:

- `B` for the overloads in namespace `std`.
- `{ last, B }` for the overloads in namespace `ranges`.

**5** *Complexity:* At most `(last - first) / 2` swaps.

```cpp
template<class BidirectionalIterator>
  constexpr bool prev_permutation(BidirectionalIterator first,
                                  BidirectionalIterator last);

template<class BidirectionalIterator, class Compare>
  constexpr bool prev_permutation(BidirectionalIterator first,
                                  BidirectionalIterator last, Compare comp);

template<bidirectional_iterator I, sentinel_for<I> S, class Comp = ranges::less,
         class Proj = identity>
  requires sortable<I, Comp, Proj>
  constexpr ranges::prev_permutation_result<I>
    ranges::prev_permutation(I first, S last, Comp comp = {}, Proj proj = {});
template<bidirectional_range R, class Comp = ranges::less,
         class Proj = identity>
  requires sortable<iterator_t<R>, Comp, Proj>
  constexpr ranges::prev_permutation_result<borrowed_iterator_t<R>>
    ranges::prev_permutation(R&& r, Comp comp = {}, Proj proj = {});
```

**6** Let `comp` be `less{}` and `proj` be `identity{}` for overloads with no parameters by those names.

**7** *Preconditions:* For the overloads in namespace `std`, `BidirectionalIterator` meets the *Cpp17ValueSwappable* requirements ([[swappable.requirements]](../library/swappable.requirements.md)).

**8** *Effects:* Takes a sequence defined by the range `[first, last)` and transforms it into the previous permutation. The previous permutation is found by assuming that the set of all permutations is lexicographically sorted with respect to `comp` and `proj`. If no such permutation exists, transforms the sequence into the last permutation; that is, the descendingly-sorted one.

**9** *Returns:* Let `B` be `true` if a previous permutation was found and otherwise `false`. Returns:

- `B` for the overloads in namespace `std`.
- `{ last, B }` for the overloads in namespace `ranges`.

**10** *Complexity:* At most `(last - first) / 2` swaps.
