# 25.8.8 Minimum and maximum [alg.min.max]

[Contents](../README.md) · up: [25.8 Sorting and related operations \[alg.sorting\]](alg.sorting.md) · prev: [25.8.7.5 `is_heap` \[is.heap\]](is.heap.md) · next: [25.8.9 Bounded value \[alg.clamp\]](alg.clamp.md)

```cpp
template<class T>
  constexpr const T& min(const T& a, const T& b);
template<class T, class Compare>
  constexpr const T& min(const T& a, const T& b, Compare comp);

template<class T, class Proj = identity,
         indirect_strict_weak_order<projected<const T*, Proj>> Comp = ranges::less>
  constexpr const T& ranges::min(const T& a, const T& b, Comp comp = {}, Proj proj = {});
```

**1** *Preconditions:* For the first form, `T` meets the *Cpp17LessThanComparable* requirements ([Table 26](utility.arg.requirements.md#tab:cpp17.lessthancomparable)).

**2** *Returns:* The smaller value.

**3** *Remarks:* Returns the first argument when the arguments are equivalent. An invocation may explicitly specify an argument for the template parameter `T` of the overloads in namespace `std`.

**4** *Complexity:* Exactly one comparison and two applications of the projection, if any.

```cpp
template<class T>
  constexpr T min(initializer_list<T> r);
template<class T, class Compare>
  constexpr T min(initializer_list<T> r, Compare comp);

template<copyable T, class Proj = identity,
         indirect_strict_weak_order<projected<const T*, Proj>> Comp = ranges::less>
  constexpr T ranges::min(initializer_list<T> r, Comp comp = {}, Proj proj = {});
template<input_range R, class Proj = identity,
         indirect_strict_weak_order<projected<iterator_t<R>, Proj>> Comp = ranges::less>
  requires indirectly_copyable_storable<iterator_t<R>, range_value_t<R>*>
  constexpr range_value_t<R>
    ranges::min(R&& r, Comp comp = {}, Proj proj = {});
```

**5** *Preconditions:* `ranges::distance(r) > 0`. For the overloads in namespace `std`, `T` meets the *Cpp17CopyConstructible* requirements. For the first form, `T` meets the *Cpp17LessThanComparable* requirements ([Table 26](utility.arg.requirements.md#tab:cpp17.lessthancomparable)).

**6** *Returns:* The smallest value in the input range.

**7** *Remarks:* Returns a copy of the leftmost element when several elements are equivalent to the smallest. An invocation may explicitly specify an argument for the template parameter `T` of the overloads in namespace `std`.

**8** *Complexity:* Exactly `ranges::distance(r) - 1` comparisons and twice as many applications of the projection, if any.

```cpp
template<class T>
  constexpr const T& max(const T& a, const T& b);
template<class T, class Compare>
  constexpr const T& max(const T& a, const T& b, Compare comp);

template<class T, class Proj = identity,
         indirect_strict_weak_order<projected<const T*, Proj>> Comp = ranges::less>
  constexpr const T& ranges::max(const T& a, const T& b, Comp comp = {}, Proj proj = {});
```

**9** *Preconditions:* For the first form, `T` meets the *Cpp17LessThanComparable* requirements ([Table 26](utility.arg.requirements.md#tab:cpp17.lessthancomparable)).

**10** *Returns:* The larger value.

**11** *Remarks:* Returns the first argument when the arguments are equivalent. An invocation may explicitly specify an argument for the template parameter `T` of the overloads in namespace `std`.

**12** *Complexity:* Exactly one comparison and two applications of the projection, if any.

```cpp
template<class T>
  constexpr T max(initializer_list<T> r);
template<class T, class Compare>
  constexpr T max(initializer_list<T> r, Compare comp);

template<copyable T, class Proj = identity,
         indirect_strict_weak_order<projected<const T*, Proj>> Comp = ranges::less>
  constexpr T ranges::max(initializer_list<T> r, Comp comp = {}, Proj proj = {});
template<input_range R, class Proj = identity,
         indirect_strict_weak_order<projected<iterator_t<R>, Proj>> Comp = ranges::less>
  requires indirectly_copyable_storable<iterator_t<R>, range_value_t<R>*>
  constexpr range_value_t<R>
    ranges::max(R&& r, Comp comp = {}, Proj proj = {});
```

**13** *Preconditions:* `ranges::distance(r) > 0`. For the overloads in namespace `std`, `T` meets the *Cpp17CopyConstructible* requirements. For the first form, `T` meets the *Cpp17LessThanComparable* requirements ([Table 26](utility.arg.requirements.md#tab:cpp17.lessthancomparable)).

**14** *Returns:* The largest value in the input range.

**15** *Remarks:* Returns a copy of the leftmost element when several elements are equivalent to the largest. An invocation may explicitly specify an argument for the template parameter `T` of the overloads in namespace `std`.

**16** *Complexity:* Exactly `ranges::distance(r) - 1` comparisons and twice as many applications of the projection, if any.

```cpp
template<class T>
  constexpr pair<const T&, const T&> minmax(const T& a, const T& b);
template<class T, class Compare>
  constexpr pair<const T&, const T&> minmax(const T& a, const T& b, Compare comp);

template<class T, class Proj = identity,
         indirect_strict_weak_order<projected<const T*, Proj>> Comp = ranges::less>
  constexpr ranges::minmax_result<const T&>
    ranges::minmax(const T& a, const T& b, Comp comp = {}, Proj proj = {});
```

**17** *Preconditions:* For the first form, `T` meets the *Cpp17LessThanComparable* requirements ([Table 26](utility.arg.requirements.md#tab:cpp17.lessthancomparable)).

**18** *Returns:* `{b, a}` if `b` is smaller than `a`, and `{a, b}` otherwise.

**19** *Remarks:* An invocation may explicitly specify an argument for the template parameter `T` of the overloads in namespace `std`.

**20** *Complexity:* Exactly one comparison and two applications of the projection, if any.

```cpp
template<class T>
  constexpr pair<T, T> minmax(initializer_list<T> t);
template<class T, class Compare>
  constexpr pair<T, T> minmax(initializer_list<T> t, Compare comp);

template<copyable T, class Proj = identity,
         indirect_strict_weak_order<projected<const T*, Proj>> Comp = ranges::less>
  constexpr ranges::minmax_result<T>
    ranges::minmax(initializer_list<T> r, Comp comp = {}, Proj proj = {});
template<input_range R, class Proj = identity,
         indirect_strict_weak_order<projected<iterator_t<R>, Proj>> Comp = ranges::less>
  requires indirectly_copyable_storable<iterator_t<R>, range_value_t<R>*>
  constexpr ranges::minmax_result<range_value_t<R>>
    ranges::minmax(R&& r, Comp comp = {}, Proj proj = {});
```

**21** *Preconditions:* `ranges::distance(r) > 0`. For the overloads in namespace `std`, `T` meets the *Cpp17CopyConstructible* requirements. For the first form, type `T` meets the *Cpp17LessThanComparable* requirements ([Table 26](utility.arg.requirements.md#tab:cpp17.lessthancomparable)).

**22** *Returns:* Let `X` be the return type. Returns `X{x, y}`, where `x` is a copy of the leftmost element with the smallest value and `y` a copy of the rightmost element with the largest value in the input range.

**23** *Remarks:* An invocation may explicitly specify an argument for the template parameter `T` of the overloads in namespace `std`.

**24** *Complexity:* At most (3/2)`ranges::distance(r)` applications of the corresponding predicate and twice as many applications of the projection, if any.

```cpp
template<class ForwardIterator>
  constexpr ForwardIterator min_element(ForwardIterator first, ForwardIterator last);

template<class ExecutionPolicy, class ForwardIterator>
  ForwardIterator min_element(ExecutionPolicy&& exec,
                              ForwardIterator first, ForwardIterator last);

template<class ForwardIterator, class Compare>
  constexpr ForwardIterator min_element(ForwardIterator first, ForwardIterator last,
                                        Compare comp);
template<class ExecutionPolicy, class ForwardIterator, class Compare>
  ForwardIterator min_element(ExecutionPolicy&& exec,
                              ForwardIterator first, ForwardIterator last,
                              Compare comp);

template<forward_iterator I, sentinel_for<I> S, class Proj = identity,
         indirect_strict_weak_order<projected<I, Proj>> Comp = ranges::less>
  constexpr I ranges::min_element(I first, S last, Comp comp = {}, Proj proj = {});
template<forward_range R, class Proj = identity,
         indirect_strict_weak_order<projected<iterator_t<R>, Proj>> Comp = ranges::less>
  constexpr borrowed_iterator_t<R>
    ranges::min_element(R&& r, Comp comp = {}, Proj proj = {});
```

**25** Let `comp` be `less{}` and `proj` be `identity{}` for the overloads with no parameters by those names.

**26** *Returns:* The first iterator `i` in the range `[first, last)` such that for every iterator `j` in the range `[first, last)`,

```cpp
bool(invoke(comp, invoke(proj, *j), invoke(proj, *i)))
```

is `false`. Returns `last` if `first == last`.

**27** *Complexity:* Exactly max(`last - first - 1`, 0) comparisons and twice as many projections.

```cpp
template<class ForwardIterator>
  constexpr ForwardIterator max_element(ForwardIterator first, ForwardIterator last);
template<class ExecutionPolicy, class ForwardIterator>
  ForwardIterator max_element(ExecutionPolicy&& exec,
                              ForwardIterator first, ForwardIterator last);

template<class ForwardIterator, class Compare>
  constexpr ForwardIterator max_element(ForwardIterator first, ForwardIterator last,
                                        Compare comp);
template<class ExecutionPolicy, class ForwardIterator, class Compare>
  ForwardIterator max_element(ExecutionPolicy&& exec,
                              ForwardIterator first, ForwardIterator last,
                              Compare comp);

template<forward_iterator I, sentinel_for<I> S, class Proj = identity,
         indirect_strict_weak_order<projected<I, Proj>> Comp = ranges::less>
  constexpr I ranges::max_element(I first, S last, Comp comp = {}, Proj proj = {});
template<forward_range R, class Proj = identity,
         indirect_strict_weak_order<projected<iterator_t<R>, Proj>> Comp = ranges::less>
  constexpr borrowed_iterator_t<R>
    ranges::max_element(R&& r, Comp comp = {}, Proj proj = {});
```

**28** Let `comp` be `less{}` and `proj` be `identity{}` for the overloads with no parameters by those names.

**29** *Returns:* The first iterator `i` in the range `[first, last)` such that for every iterator `j` in the range `[first, last)`,

```cpp
bool(invoke(comp, invoke(proj, *i), invoke(proj, *j)))
```

is `false`. Returns `last` if `first == last`.

**30** *Complexity:* Exactly max(`last - first - 1`, 0) comparisons and twice as many projections.

```cpp
template<class ForwardIterator>
  constexpr pair<ForwardIterator, ForwardIterator>
    minmax_element(ForwardIterator first, ForwardIterator last);
template<class ExecutionPolicy, class ForwardIterator>
  pair<ForwardIterator, ForwardIterator>
    minmax_element(ExecutionPolicy&& exec,
                   ForwardIterator first, ForwardIterator last);

template<class ForwardIterator, class Compare>
  constexpr pair<ForwardIterator, ForwardIterator>
    minmax_element(ForwardIterator first, ForwardIterator last, Compare comp);
template<class ExecutionPolicy, class ForwardIterator, class Compare>
  pair<ForwardIterator, ForwardIterator>
    minmax_element(ExecutionPolicy&& exec,
                   ForwardIterator first, ForwardIterator last, Compare comp);

template<forward_iterator I, sentinel_for<I> S, class Proj = identity,
         indirect_strict_weak_order<projected<I, Proj>> Comp = ranges::less>
  constexpr ranges::minmax_result<I>
    ranges::minmax_element(I first, S last, Comp comp = {}, Proj proj = {});
template<forward_range R, class Proj = identity,
         indirect_strict_weak_order<projected<iterator_t<R>, Proj>> Comp = ranges::less>
  constexpr ranges::minmax_result<borrowed_iterator_t<R>>
    ranges::minmax_element(R&& r, Comp comp = {}, Proj proj = {});
```

**31** *Returns:* `{first, first}` if `[first, last)` is empty, otherwise `{m, M}`, where `m` is the first iterator in `[first, last)` such that no iterator in the range refers to a smaller element, and where `M` is the last iterator[^1] in `[first, last)` such that no iterator in the range refers to a larger element.

**32** *Complexity:* Let *N* be `last - first`. At most $\max(\bigl\lfloor{\frac{3}{2}} (N-1)\bigr\rfloor, 0)$ comparisons and twice as many applications of the projection, if any.

[^1]: This behavior intentionally differs from `max_element`.
