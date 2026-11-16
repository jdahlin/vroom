# 25.7.8 Remove [alg.remove]

[Contents](../../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7.7 Generate \[alg.generate\]](alg.generate.md) · next: [25.7.9 Unique \[alg.unique\]](alg.unique.md)

```cpp
template<class ForwardIterator, class T>
  constexpr ForwardIterator remove(ForwardIterator first, ForwardIterator last,
                                   const T& value);
template<class ExecutionPolicy, class ForwardIterator, class T>
  ForwardIterator remove(ExecutionPolicy&& exec,
                         ForwardIterator first, ForwardIterator last,
                         const T& value);

template<class ForwardIterator, class Predicate>
  constexpr ForwardIterator remove_if(ForwardIterator first, ForwardIterator last,
                                      Predicate pred);
template<class ExecutionPolicy, class ForwardIterator, class Predicate>
  ForwardIterator remove_if(ExecutionPolicy&& exec,
                            ForwardIterator first, ForwardIterator last,
                            Predicate pred);

template<permutable I, sentinel_for<I> S, class T, class Proj = identity>
  requires indirect_binary_predicate<ranges::equal_to, projected<I, Proj>, const T*>
  constexpr subrange<I> ranges::remove(I first, S last, const T& value, Proj proj = {});
template<forward_range R, class T, class Proj = identity>
  requires permutable<iterator_t<R>> &&
           indirect_binary_predicate<ranges::equal_to, projected<iterator_t<R>, Proj>, const T*>
  constexpr borrowed_subrange_t<R>
    ranges::remove(R&& r, const T& value, Proj proj = {});
template<permutable I, sentinel_for<I> S, class Proj = identity,
         indirect_unary_predicate<projected<I, Proj>> Pred>
  constexpr subrange<I> ranges::remove_if(I first, S last, Pred pred, Proj proj = {});
template<forward_range R, class Proj = identity,
         indirect_unary_predicate<projected<iterator_t<R>, Proj>> Pred>
  requires permutable<iterator_t<R>>
  constexpr borrowed_subrange_t<R>
    ranges::remove_if(R&& r, Pred pred, Proj proj = {});
```

**1** Let *E* be

- `bool(*i == value)` for `remove`;
- `bool(pred(*i))` for `remove_if`;
- `bool(invoke(proj, *i) == value)` for `ranges::remove`;
- `bool(invoke(pred, invoke(proj, *i)))` for `ranges::remove_if`.

**2** *Preconditions:* For the algorithms in namespace `std`, the type of `*first` meets the *Cpp17MoveAssignable* requirements ([Table 30](../library/utility.arg.requirements.md#tab:cpp17.moveassignable)).

**3** *Effects:* Eliminates all the elements referred to by iterator `i` in the range `[first, last)` for which *E* holds.

**4** *Returns:* Let *j* be the end of the resulting range. Returns:

- *j* for the overloads in namespace `std`.
- `{j, last}` for the overloads in namespace `ranges`.

**5** *Remarks:* Stable ([[algorithm.stable]](../library/algorithm.stable.md)).

**6** *Complexity:* Exactly `last - first` applications of the corresponding predicate and any projection.

**7** [*Note 1*: Each element in the range `[ret, last)`, where `ret` is the returned value, has a valid but unspecified state, because the algorithms can eliminate elements by moving from elements that were originally in that range. — *end note*]

```cpp
template<class InputIterator, class OutputIterator, class T>
  constexpr OutputIterator
    remove_copy(InputIterator first, InputIterator last,
                OutputIterator result, const T& value);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
         class T>
  ForwardIterator2
    remove_copy(ExecutionPolicy&& exec,
                ForwardIterator1 first, ForwardIterator1 last,
                ForwardIterator2 result, const T& value);

template<class InputIterator, class OutputIterator, class Predicate>
  constexpr OutputIterator
    remove_copy_if(InputIterator first, InputIterator last,
                   OutputIterator result, Predicate pred);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
         class Predicate>
  ForwardIterator2
    remove_copy_if(ExecutionPolicy&& exec,
                   ForwardIterator1 first, ForwardIterator1 last,
                   ForwardIterator2 result, Predicate pred);

template<input_iterator I, sentinel_for<I> S, weakly_incrementable O, class T,
         class Proj = identity>
  requires indirectly_copyable<I, O> &&
           indirect_binary_predicate<ranges::equal_to, projected<I, Proj>, const T*>
  constexpr ranges::remove_copy_result<I, O>
    ranges::remove_copy(I first, S last, O result, const T& value, Proj proj = {});
template<input_range R, weakly_incrementable O, class T, class Proj = identity>
  requires indirectly_copyable<iterator_t<R>, O> &&
           indirect_binary_predicate<ranges::equal_to, projected<iterator_t<R>, Proj>, const T*>
  constexpr ranges::remove_copy_result<borrowed_iterator_t<R>, O>
    ranges::remove_copy(R&& r, O result, const T& value, Proj proj = {});
template<input_iterator I, sentinel_for<I> S, weakly_incrementable O,
         class Proj = identity, indirect_unary_predicate<projected<I, Proj>> Pred>
  requires indirectly_copyable<I, O>
  constexpr ranges::remove_copy_if_result<I, O>
    ranges::remove_copy_if(I first, S last, O result, Pred pred, Proj proj = {});
template<input_range R, weakly_incrementable O, class Proj = identity,
         indirect_unary_predicate<projected<iterator_t<R>, Proj>> Pred>
  requires indirectly_copyable<iterator_t<R>, O>
  constexpr ranges::remove_copy_if_result<borrowed_iterator_t<R>, O>
    ranges::remove_copy_if(R&& r, O result, Pred pred, Proj proj = {});
```

**8** Let *E* be

- `bool(*i == value)` for `remove_copy`;
- `bool(pred(*i))` for `remove_copy_if`;
- `bool(invoke(proj, *i) == value)` for `ranges::remove_copy`;
- `bool(invoke(pred, invoke(proj, *i)))` for `ranges::remove_copy_if`.

**9** Let *N* be the number of elements in `[first, last)` for which *E* is `false`.

**10** *Mandates:* `*first` is writable ([[iterator.requirements.general]](../iterators/iterator.requirements.general.md)) to `result`.

**11** *Preconditions:* The ranges `[first, last)` and `[result, result + (last - first))` do not overlap. [*Note 2*: For the overloads with an `ExecutionPolicy`, there may be a performance cost if `iterator_traits<ForwardIterator1>::value_type` does not meet the *Cpp17MoveConstructible* ([Table 28](../library/utility.arg.requirements.md#tab:cpp17.moveconstructible)) requirements. — *end note*]

**12** *Effects:* Copies all the elements referred to by the iterator `i` in the range `[first, last)` for which *E* is `false`.

**13** *Returns:*

- `result + N`, for the algorithms in namespace `std`.
- `{last, result + N}`, for the algorithms in namespace `ranges`.

**14** *Complexity:* Exactly `last - first` applications of the corresponding predicate and any projection.

**15** *Remarks:* Stable ([[algorithm.stable]](../library/algorithm.stable.md)).
