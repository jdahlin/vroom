# 25.7.11 Rotate [alg.rotate]

[Contents](../../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7.10 Reverse \[alg.reverse\]](alg.reverse.md) · next: [25.7.12 Sample \[alg.random.sample\]](alg.random.sample.md)

```cpp
template<class ForwardIterator>
  constexpr ForwardIterator
    rotate(ForwardIterator first, ForwardIterator middle, ForwardIterator last);
template<class ExecutionPolicy, class ForwardIterator>
  ForwardIterator
    rotate(ExecutionPolicy&& exec,
           ForwardIterator first, ForwardIterator middle, ForwardIterator last);

template<permutable I, sentinel_for<I> S>
  constexpr subrange<I> ranges::rotate(I first, I middle, S last);
```

**1** *Preconditions:* `[first, middle)` and `[middle, last)` are valid ranges. For the overloads in namespace `std`, `ForwardIterator` meets the *Cpp17ValueSwappable* requirements ([[swappable.requirements]](../library/swappable.requirements.md)), and the type of `*first` meets the *Cpp17MoveConstructible* ([Table 28](../library/utility.arg.requirements.md#tab:cpp17.moveconstructible)) and *Cpp17MoveAssignable* ([Table 30](../library/utility.arg.requirements.md#tab:cpp17.moveassignable)) requirements.

**2** *Effects:* For each non-negative integer `i < (last - first)`, places the element from the position `first + i` into position `first + (i + (last - middle)) % (last - first)`. [*Note 1*: This is a left rotate. — *end note*]

**3** *Returns:*

- `first + (last - middle)` for the overloads in namespace `std`.
- `{first + (last - middle), last}` for the overload in namespace `ranges`.

**4** *Complexity:* At most `last - first` swaps.

```cpp
template<forward_range R>
  requires permutable<iterator_t<R>>
  constexpr borrowed_subrange_t<R> ranges::rotate(R&& r, iterator_t<R> middle);
```

**5** *Effects:* Equivalent to: `return ranges::rotate(ranges::begin(r), middle, ranges::end(r));`

```cpp
template<class ForwardIterator, class OutputIterator>
  constexpr OutputIterator
    rotate_copy(ForwardIterator first, ForwardIterator middle, ForwardIterator last,
                OutputIterator result);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2>
  ForwardIterator2
    rotate_copy(ExecutionPolicy&& exec,
                ForwardIterator1 first, ForwardIterator1 middle, ForwardIterator1 last,
                ForwardIterator2 result);

  template<forward_iterator I, sentinel_for<I> S, weakly_incrementable O>
    requires indirectly_copyable<I, O>
    constexpr ranges::rotate_copy_result<I, O>
      ranges::rotate_copy(I first, I middle, S last, O result);
```

**6** Let *N* be `last - first`.

**7** *Preconditions:* `[first, middle)` and `[middle, last)` are valid ranges. The ranges `[first, last)` and `[result, result + N)` do not overlap.

**8** *Effects:* Copies the range `[first, last)` to the range `[result, result + N)` such that for each non-negative integer *i* &lt; *N* the following assignment takes place: `*(result + i) = *(first + (i + (middle - first)) % N)`.

**9** *Returns:*

- `result + N` for the overloads in namespace `std`.
- `{last, result + N}` for the overload in namespace `ranges`.

**10** *Complexity:* Exactly *N* assignments.

```cpp
template<forward_range R, weakly_incrementable O>
  requires indirectly_copyable<iterator_t<R>, O>
  constexpr ranges::rotate_copy_result<borrowed_iterator_t<R>, O>
    ranges::rotate_copy(R&& r, iterator_t<R> middle, O result);
```

**11** *Effects:* Equivalent to:

```cpp
return ranges::rotate_copy(ranges::begin(r), middle, ranges::end(r), result);
```
