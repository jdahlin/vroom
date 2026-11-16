# 22.3.9.6 Operations [forwardlist.ops]

[Contents](../../README.md) · up: [22.3.9 Class template `forward_list` \[forwardlist\]](forwardlist.md) · prev: [22.3.9.5 Modifiers \[forwardlist.modifiers\]](forwardlist.modifiers.md) · next: [22.3.9.7 Erasure \[forward.list.erasure\]](forward.list.erasure.md)

**1** In this subclause, arguments for a template parameter named `Predicate` or `BinaryPredicate` shall meet the corresponding requirements in [[algorithms.requirements]](../algorithms/algorithms.requirements.md). For `merge` and `sort`, the definitions and requirements in [[alg.sorting]](../algorithms/alg.sorting.md) apply.

```cpp
void splice_after(const_iterator position, forward_list& x);
void splice_after(const_iterator position, forward_list&& x);
```

**2** *Preconditions:* `position` is `before_begin()` or is a dereferenceable iterator in the range `[begin(), end())`. `get_allocator() == x.get_allocator()` is `true`. `addressof(x) != this` is `true`.

**3** *Effects:* Inserts the contents of `x` after `position`, and `x` becomes empty. Pointers and references to the moved elements of `x` now refer to those same elements but as members of `*this`. Iterators referring to the moved elements will continue to refer to their elements, but they now behave as iterators into `*this`, not into `x`.

**4** *Throws:* Nothing.

**5** *Complexity:* 𝑂(`distance(x.begin(), x.end())`)

```cpp
void splice_after(const_iterator position, forward_list& x, const_iterator i);
void splice_after(const_iterator position, forward_list&& x, const_iterator i);
```

**6** *Preconditions:* `position` is `before_begin()` or is a dereferenceable iterator in the range `[begin(), end())`. The iterator following `i` is a dereferenceable iterator in `x`. `get_allocator() == x.get_allocator()` is `true`.

**7** *Effects:* Inserts the element following `i` into `*this`, following `position`, and removes it from `x`. The result is unchanged if `position == i` or `position == ++i`. Pointers and references to `*++i` continue to refer to the same element but as a member of `*this`. Iterators to `*++i` continue to refer to the same element, but now behave as iterators into `*this`, not into `x`.

**8** *Throws:* Nothing.

**9** *Complexity:* 𝑂(1)

```cpp
void splice_after(const_iterator position, forward_list& x,
                  const_iterator first, const_iterator last);
void splice_after(const_iterator position, forward_list&& x,
                  const_iterator first, const_iterator last);
```

**10** *Preconditions:* `position` is `before_begin()` or is a dereferenceable iterator in the range `[begin(), end())`. `(first, last)` is a valid range in `x`, and all iterators in the range `(first, last)` are dereferenceable. `position` is not an iterator in the range `(first, last)`. `get_allocator() == x.get_allocator()` is `true`.

**11** *Effects:* Inserts elements in the range `(first, last)` after `position` and removes the elements from `x`. Pointers and references to the moved elements of `x` now refer to those same elements but as members of `*this`. Iterators referring to the moved elements will continue to refer to their elements, but they now behave as iterators into `*this`, not into `x`.

**12** *Complexity:* 𝑂(`distance(first, last)`)

```cpp
size_type remove(const T& value);
template<class Predicate> size_type remove_if(Predicate pred);
```

**13** *Effects:* Erases all the elements in the list referred to by a list iterator `i` for which the following conditions hold: `*i == value` (for `remove()`), `pred(*i)` is `true` (for `remove_if()`). Invalidates only the iterators and references to the erased elements.

**14** *Returns:* The number of elements erased.

**15** *Throws:* Nothing unless an exception is thrown by the equality comparison or the predicate.

**16** *Remarks:* Stable ([[algorithm.stable]](../library/algorithm.stable.md)).

**17** *Complexity:* Exactly `distance(begin(), end())` applications of the corresponding predicate.

```cpp
size_type unique();
template<class BinaryPredicate> size_type unique(BinaryPredicate pred);
```

**18** *Effects:* Erases all but the first element from every consecutive group of equal elements referred to by the iterator `i` in the range `[first + 1, last)` for which `*i == *(i-1)` (for the version with no arguments) or `pred(*i, *(i - 1))` (for the version with a predicate argument) holds. Invalidates only the iterators and references to the erased elements.

**19** *Returns:* The number of elements erased.

**20** *Throws:* Nothing unless an exception is thrown by the equality comparison or the predicate.

**21** *Complexity:* If the range `[first, last)` is not empty, exactly `(last - first) - 1` applications of the corresponding predicate, otherwise no applications of the predicate.

```cpp
void merge(forward_list& x);
void merge(forward_list&& x);
template<class Compare> void merge(forward_list& x, Compare comp);
template<class Compare> void merge(forward_list&& x, Compare comp);
```

**22** *Preconditions:* `*this` and `x` are both sorted with respect to the comparator `operator<` (for the first two overloads) or `comp` (for the last two overloads), and `get_allocator() == x.get_allocator()` is `true`.

**23** *Effects:* Merges the two sorted ranges `[begin(), end())` and `[x.begin(), x.end())`. `x` is empty after the merge. If an exception is thrown other than by a comparison there are no effects. Pointers and references to the moved elements of `x` now refer to those same elements but as members of `*this`. Iterators referring to the moved elements will continue to refer to their elements, but they now behave as iterators into `*this`, not into `x`.

**24** *Remarks:* Stable ([[algorithm.stable]](../library/algorithm.stable.md)).

**25** *Complexity:* At most `distance(begin(), end()) + distance(x.begin(), x.end()) - 1` comparisons.

```cpp
void sort();
template<class Compare> void sort(Compare comp);
```

**26** *Effects:* Sorts the list according to the `operator<` or the `comp` function object. If an exception is thrown, the order of the elements in `*this` is unspecified. Does not affect the validity of iterators and references.

**27** *Remarks:* Stable ([[algorithm.stable]](../library/algorithm.stable.md)).

**28** *Complexity:* Approximately *N* log *N* comparisons, where *N* is `distance(begin(), end())`.

```cpp
void reverse() noexcept;
```

**29** *Effects:* Reverses the order of the elements in the list. Does not affect the validity of iterators and references.

**30** *Complexity:* Linear time.
