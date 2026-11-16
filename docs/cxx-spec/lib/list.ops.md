# 22.3.10.5 Operations [list.ops]

[Contents](../README.md) · up: [22.3.10 Class template `list` \[list\]](list.md) · prev: [22.3.10.4 Modifiers \[list.modifiers\]](list.modifiers.md) · next: [22.3.10.6 Erasure \[list.erasure\]](list.erasure.md)

**1** Since lists allow fast insertion and erasing from the middle of a list, certain operations are provided specifically for them.[^1] In this subclause, arguments for a template parameter named `Predicate` or `BinaryPredicate` shall meet the corresponding requirements in [[algorithms.requirements]](algorithms.requirements.md). For `merge` and `sort`, the definitions and requirements in [[alg.sorting]](alg.sorting.md) apply.

**2** `list` provides three splice operations that destructively move elements from one list to another. The behavior of splice operations is undefined if `get_allocator() != x.get_allocator()`.

```cpp
void splice(const_iterator position, list& x);
void splice(const_iterator position, list&& x);
```

**3** *Preconditions:* `addressof(x) != this` is `true`.

**4** *Effects:* Inserts the contents of `x` before `position` and `x` becomes empty. Pointers and references to the moved elements of `x` now refer to those same elements but as members of `*this`. Iterators referring to the moved elements will continue to refer to their elements, but they now behave as iterators into `*this`, not into `x`.

**5** *Throws:* Nothing.

**6** *Complexity:* Constant time.

```cpp
void splice(const_iterator position, list& x, const_iterator i);
void splice(const_iterator position, list&& x, const_iterator i);
```

**7** *Preconditions:* `i` is a valid dereferenceable iterator of `x`.

**8** *Effects:* Inserts an element pointed to by `i` from list `x` before `position` and removes the element from `x`. The result is unchanged if `position == i` or `position == ++i`. Pointers and references to `*i` continue to refer to this same element but as a member of `*this`. Iterators to `*i` (including `i` itself) continue to refer to the same element, but now behave as iterators into `*this`, not into `x`.

**9** *Throws:* Nothing.

**10** *Complexity:* Constant time.

```cpp
void splice(const_iterator position, list& x, const_iterator first,
            const_iterator last);
void splice(const_iterator position, list&& x, const_iterator first,
            const_iterator last);
```

**11** *Preconditions:* `[first, last)` is a valid range in `x`. `position` is not an iterator in the range `[first, last)`.

**12** *Effects:* Inserts elements in the range `[first, last)` before `position` and removes the elements from `x`. Pointers and references to the moved elements of `x` now refer to those same elements but as members of `*this`. Iterators referring to the moved elements will continue to refer to their elements, but they now behave as iterators into `*this`, not into `x`.

**13** *Throws:* Nothing.

**14** *Complexity:* Constant time if `addressof(x) == this`; otherwise, linear time.

```cpp
size_type remove(const T& value);
template<class Predicate> size_type remove_if(Predicate pred);
```

**15** *Effects:* Erases all the elements in the list referred to by a list iterator `i` for which the following conditions hold: `*i == value`, `pred(*i) != false`. Invalidates only the iterators and references to the erased elements.

**16** *Returns:* The number of elements erased.

**17** *Throws:* Nothing unless an exception is thrown by `*i == value` or `pred(*i) != false`.

**18** *Remarks:* Stable ([[algorithm.stable]](algorithm.stable.md)).

**19** *Complexity:* Exactly `size()` applications of the corresponding predicate.

```cpp
size_type unique();
template<class BinaryPredicate> size_type unique(BinaryPredicate binary_pred);
```

**20** *Effects:* Erases all but the first element from every consecutive group of equal elements referred to by the iterator `i` in the range `[first + 1, last)` for which `*i == *(i-1)` (for the version of `unique` with no arguments) or `pred(*i, *(i - 1))` (for the version of `unique` with a predicate argument) holds. Invalidates only the iterators and references to the erased elements.

**21** *Returns:* The number of elements erased.

**22** *Throws:* Nothing unless an exception is thrown by `*i == *(i-1)` or `pred(*i, *(i - 1))`

**23** *Complexity:* If the range `[first, last)` is not empty, exactly `(last - first) - 1` applications of the corresponding predicate, otherwise no applications of the predicate.

```cpp
void merge(list& x);
void merge(list&& x);
template<class Compare> void merge(list& x, Compare comp);
template<class Compare> void merge(list&& x, Compare comp);
```

**24** *Preconditions:* Both the list and the argument list shall be sorted with respect to the comparator `operator<` (for the first two overloads) or `comp` (for the last two overloads), and `get_allocator() == x.get_allocator()` is `true`.

**25** *Effects:* If `addressof(x) == this`, does nothing; otherwise, merges the two sorted ranges `[begin(), end())` and `[x.begin(), x.end())`. The result is a range in which the elements will be sorted in non-decreasing order according to the ordering defined by `comp`; that is, for every iterator `i`, in the range other than the first, the condition `comp(*i, *(i - 1))` will be `false`. Pointers and references to the moved elements of `x` now refer to those same elements but as members of `*this`. Iterators referring to the moved elements will continue to refer to their elements, but they now behave as iterators into `*this`, not into `x`.

**26** *Remarks:* Stable ([[algorithm.stable]](algorithm.stable.md)). If `addressof(x) != this`, the range `[x.begin(), x.end())` is empty after the merge. No elements are copied by this operation.

**27** *Complexity:* At most `size() + x.size() - 1` applications of `comp` if `addressof(x) != this`; otherwise, no applications of `comp` are performed. If an exception is thrown other than by a comparison there are no effects.

```cpp
void reverse() noexcept;
```

**28** *Effects:* Reverses the order of the elements in the list. Does not affect the validity of iterators and references.

**29** *Complexity:* Linear time.

```cpp
void sort();
template<class Compare> void sort(Compare comp);
```

**30** *Effects:* Sorts the list according to the `operator<` or a `Compare` function object. If an exception is thrown, the order of the elements in `*this` is unspecified. Does not affect the validity of iterators and references.

**31** *Remarks:* Stable ([[algorithm.stable]](algorithm.stable.md)).

**32** *Complexity:* Approximately *N* log *N* comparisons, where `N == size()`.

[^1]: As specified in [[allocator.requirements]](allocator.requirements.md), the requirements in this Clause apply only to lists whose allocators compare equal.
