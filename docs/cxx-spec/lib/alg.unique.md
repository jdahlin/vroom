# 25.7.9 Unique [alg.unique]

[Contents](../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7.8 Remove \[alg.remove\]](alg.remove.md) · next: [25.7.10 Reverse \[alg.reverse\]](alg.reverse.md)

```cpp
template<class ForwardIterator>
  constexpr ForwardIterator unique(ForwardIterator first, ForwardIterator last);
template<class ExecutionPolicy, class ForwardIterator>
  ForwardIterator unique(ExecutionPolicy&& exec,
                         ForwardIterator first, ForwardIterator last);

template<class ForwardIterator, class BinaryPredicate>
  constexpr ForwardIterator unique(ForwardIterator first, ForwardIterator last,
                                   BinaryPredicate pred);
template<class ExecutionPolicy, class ForwardIterator, class BinaryPredicate>
  ForwardIterator unique(ExecutionPolicy&& exec,
                         ForwardIterator first, ForwardIterator last,
                         BinaryPredicate pred);

template<permutable I, sentinel_for<I> S, class Proj = identity,
         indirect_equivalence_relation<projected<I, Proj>> C = ranges::equal_to>
  constexpr subrange<I> ranges::unique(I first, S last, C comp = {}, Proj proj = {});
template<forward_range R, class Proj = identity,
         indirect_equivalence_relation<projected<iterator_t<R>, Proj>> C = ranges::equal_to>
  requires permutable<iterator_t<R>>
  constexpr borrowed_subrange_t<R>
    ranges::unique(R&& r, C comp = {}, Proj proj = {});
```

**1** Let `pred` be `equal_to{}` for the overloads with no parameter `pred`, and let *E* be

- `bool(pred(*(i - 1), *i))` for the overloads in namespace `std`;
- `bool(invoke(comp, invoke(proj, *(i - 1)), invoke(proj, *i)))` for the overloads in namespace `ranges`.

**2** *Preconditions:* For the overloads in namepace `std`, `pred` is an equivalence relation and the type of `*first` meets the *Cpp17MoveAssignable* requirements ([Table 30](utility.arg.requirements.md#tab:cpp17.moveassignable)).

**3** *Effects:* For a nonempty range, eliminates all but the first element from every consecutive group of equivalent elements referred to by the iterator `i` in the range `[first + 1, last)` for which *E* is `true`.

**4** *Returns:* Let *j* be the end of the resulting range. Returns:

- *j* for the overloads in namespace `std`.
- `{j, last}` for the overloads in namespace `ranges`.

**5** *Complexity:* For nonempty ranges, exactly `(last - first) - 1` applications of the corresponding predicate and no more than twice as many applications of any projection.

```cpp
template<class InputIterator, class OutputIterator>
  constexpr OutputIterator
    unique_copy(InputIterator first, InputIterator last,
                OutputIterator result);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2>
  ForwardIterator2
    unique_copy(ExecutionPolicy&& exec,
                ForwardIterator1 first, ForwardIterator1 last,
                ForwardIterator2 result);

template<class InputIterator, class OutputIterator,
         class BinaryPredicate>
  constexpr OutputIterator
    unique_copy(InputIterator first, InputIterator last,
                OutputIterator result, BinaryPredicate pred);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
         class BinaryPredicate>
  ForwardIterator2
    unique_copy(ExecutionPolicy&& exec,
                ForwardIterator1 first, ForwardIterator1 last,
                ForwardIterator2 result, BinaryPredicate pred);

template<input_iterator I, sentinel_for<I> S, weakly_incrementable O, class Proj = identity,
         indirect_equivalence_relation<projected<I, Proj>> C = ranges::equal_to>
  requires indirectly_copyable<I, O> &&
           (forward_iterator<I> ||
            (input_iterator<O> && same_as<iter_value_t<I>, iter_value_t<O>>) ||
            indirectly_copyable_storable<I, O>)
  constexpr ranges::unique_copy_result<I, O>
    ranges::unique_copy(I first, S last, O result, C comp = {}, Proj proj = {});
template<input_range R, weakly_incrementable O, class Proj = identity,
         indirect_equivalence_relation<projected<iterator_t<R>, Proj>> C = ranges::equal_to>
  requires indirectly_copyable<iterator_t<R>, O> &&
           (forward_iterator<iterator_t<R>> ||
            (input_iterator<O> && same_as<range_value_t<R>, iter_value_t<O>>) ||
            indirectly_copyable_storable<iterator_t<R>, O>)
  constexpr ranges::unique_copy_result<borrowed_iterator_t<R>, O>
    ranges::unique_copy(R&& r, O result, C comp = {}, Proj proj = {});
```

**6** Let `pred` be `equal_to{}` for the overloads in namespace `std` with no parameter `pred`, and let *E* be

- `bool(pred(*i, *(i - 1)))` for the overloads in namespace `std`;
- `bool(invoke(comp, invoke(proj, *i), invoke(proj, *(i - 1))))` for the overloads in namespace `ranges`.

**7** *Mandates:* `*first` is writable ([[iterator.requirements.general]](iterator.requirements.general.md)) to `result`.

**8** *Preconditions:*

- The ranges `[first, last)` and `[result, result+(last-first))` do not overlap.

- For the overloads in namespace `std`:

  - The comparison function is an equivalence relation.
  - For the overloads with no `ExecutionPolicy`, let `T` be the value type of `InputIterator`. If `InputIterator` meets the *Cpp17ForwardIterator* requirements, then there are no additional requirements for `T`. Otherwise, if `OutputIterator` meets the *Cpp17ForwardIterator* requirements and its value type is the same as `T`, then `T` meets the *Cpp17CopyAssignable* ([Table 31](utility.arg.requirements.md#tab:cpp17.copyassignable)) requirements. Otherwise, `T` meets both the *Cpp17CopyConstructible* ([Table 29](utility.arg.requirements.md#tab:cpp17.copyconstructible)) and *Cpp17CopyAssignable* requirements. [*Note 1*: For the overloads with an `ExecutionPolicy`, there may be a performance cost if the value type of `ForwardIterator1` does not meet both the *Cpp17CopyConstructible* and *Cpp17CopyAssignable* requirements. — *end note*]

**9** *Effects:* Copies only the first element from every consecutive group of equal elements referred to by the iterator `i` in the range `[first, last)` for which *E* holds.

**10** *Returns:*

- `result + N` for the overloads in namespace `std`.
- `{last, result + N}` for the overloads in namespace `ranges`.

**11** *Complexity:* Exactly `last - first - 1` applications of the corresponding predicate and no more than twice as many applications of any projection.
