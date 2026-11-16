# 25.7.5 Replace [alg.replace]

[Contents](../README.md) · up: [25.7 Mutating sequence operations \[alg.modifying.operations\]](alg.modifying.operations.md) · prev: [25.7.4 Transform \[alg.transform\]](alg.transform.md) · next: [25.7.6 Fill \[alg.fill\]](alg.fill.md)

```cpp
template<class ForwardIterator, class T>
  constexpr void replace(ForwardIterator first, ForwardIterator last,
                         const T& old_value, const T& new_value);
template<class ExecutionPolicy, class ForwardIterator, class T>
  void replace(ExecutionPolicy&& exec,
               ForwardIterator first, ForwardIterator last,
               const T& old_value, const T& new_value);

template<class ForwardIterator, class Predicate, class T>
  constexpr void replace_if(ForwardIterator first, ForwardIterator last,
                            Predicate pred, const T& new_value);
template<class ExecutionPolicy, class ForwardIterator, class Predicate, class T>
  void replace_if(ExecutionPolicy&& exec,
                  ForwardIterator first, ForwardIterator last,
                  Predicate pred, const T& new_value);

template<input_iterator I, sentinel_for<I> S, class T1, class T2, class Proj = identity>
  requires indirectly_writable<I, const T2&> &&
           indirect_binary_predicate<ranges::equal_to, projected<I, Proj>, const T1*>
  constexpr I
    ranges::replace(I first, S last, const T1& old_value, const T2& new_value, Proj proj = {});
template<input_range R, class T1, class T2, class Proj = identity>
  requires indirectly_writable<iterator_t<R>, const T2&> &&
           indirect_binary_predicate<ranges::equal_to, projected<iterator_t<R>, Proj>, const T1*>
  constexpr borrowed_iterator_t<R>
    ranges::replace(R&& r, const T1& old_value, const T2& new_value, Proj proj = {});
template<input_iterator I, sentinel_for<I> S, class T, class Proj = identity,
         indirect_unary_predicate<projected<I, Proj>> Pred>
  requires indirectly_writable<I, const T&>
  constexpr I ranges::replace_if(I first, S last, Pred pred, const T& new_value, Proj proj = {});
template<input_range R, class T, class Proj = identity,
         indirect_unary_predicate<projected<iterator_t<R>, Proj>> Pred>
  requires indirectly_writable<iterator_t<R>, const T&>
  constexpr borrowed_iterator_t<R>
    ranges::replace_if(R&& r, Pred pred, const T& new_value, Proj proj = {});
```

**1** Let *E* be

- `bool(*i == old_value)` for `replace`;
- `bool(pred(*i))` for `replace_if`;
- `bool(invoke(proj, *i) == old_value)` for `ranges::replace`;
- `bool(invoke(pred, invoke(proj, *i)))` for `ranges::replace_if`.

**2** *Mandates:* `new_value` is writable ([[iterator.requirements.general]](iterator.requirements.general.md)) to `first`.

**3** *Effects:* Substitutes elements referred by the iterator `i` in the range `[first, last)` with `new_value`, when *E* is `true`.

**4** *Returns:* `last` for the overloads in namespace `ranges`.

**5** *Complexity:* Exactly `last - first` applications of the corresponding predicate and any projection.

```cpp
template<class InputIterator, class OutputIterator, class T>
  constexpr OutputIterator
    replace_copy(InputIterator first, InputIterator last,
                 OutputIterator result,
                 const T& old_value, const T& new_value);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2, class T>
  ForwardIterator2
    replace_copy(ExecutionPolicy&& exec,
                 ForwardIterator1 first, ForwardIterator1 last,
                 ForwardIterator2 result,
                 const T& old_value, const T& new_value);

template<class InputIterator, class OutputIterator, class Predicate, class T>
  constexpr OutputIterator
    replace_copy_if(InputIterator first, InputIterator last,
                    OutputIterator result,
                    Predicate pred, const T& new_value);
template<class ExecutionPolicy, class ForwardIterator1, class ForwardIterator2,
         class Predicate, class T>
  ForwardIterator2
    replace_copy_if(ExecutionPolicy&& exec,
                    ForwardIterator1 first, ForwardIterator1 last,
                    ForwardIterator2 result,
                    Predicate pred, const T& new_value);

template<input_iterator I, sentinel_for<I> S, class T1, class T2, output_iterator<const T2&> O,
         class Proj = identity>
  requires indirectly_copyable<I, O> &&
           indirect_binary_predicate<ranges::equal_to, projected<I, Proj>, const T1*>
  constexpr ranges::replace_copy_result<I, O>
    ranges::replace_copy(I first, S last, O result, const T1& old_value, const T2& new_value,
                         Proj proj = {});
template<input_range R, class T1, class T2, output_iterator<const T2&> O,
         class Proj = identity>
  requires indirectly_copyable<iterator_t<R>, O> &&
           indirect_binary_predicate<ranges::equal_to, projected<iterator_t<R>, Proj>, const T1*>
  constexpr ranges::replace_copy_result<borrowed_iterator_t<R>, O>
    ranges::replace_copy(R&& r, O result, const T1& old_value, const T2& new_value,
                         Proj proj = {});

template<input_iterator I, sentinel_for<I> S, class T, output_iterator<const T&> O,
         class Proj = identity, indirect_unary_predicate<projected<I, Proj>> Pred>
  requires indirectly_copyable<I, O>
  constexpr ranges::replace_copy_if_result<I, O>
    ranges::replace_copy_if(I first, S last, O result, Pred pred, const T& new_value,
                            Proj proj = {});
template<input_range R, class T, output_iterator<const T&> O, class Proj = identity,
         indirect_unary_predicate<projected<iterator_t<R>, Proj>> Pred>
  requires indirectly_copyable<iterator_t<R>, O>
  constexpr ranges::replace_copy_if_result<borrowed_iterator_t<R>, O>
    ranges::replace_copy_if(R&& r, O result, Pred pred, const T& new_value,
                            Proj proj = {});
```

**6** Let *E* be

- `bool(*(first + (i - result)) == old_value)` for `replace_copy`;
- `bool(pred(*(first + (i - result))))` for `replace_copy_if`;
- `bool(invoke(proj, *(first + (i - result))) == old_value)` for `ranges::replace_copy`;
- `bool(invoke(pred, invoke(proj, *(first + (i - result)))))` for `ranges::replace_copy_if`.

**7** *Mandates:* The results of the expressions `*first` and `new_value` are writable ([[iterator.requirements.general]](iterator.requirements.general.md)) to `result`.

**8** *Preconditions:* The ranges `[first, last)` and `[result, result + (last - first))` do not overlap.

**9** *Effects:* Assigns through every iterator `i` in the range `[result, result + (last - first))` a new corresponding value

- `new_value` if *E* is `true` or
- `*(first + (i - result))` otherwise.

**10** *Returns:*

- `result + (last - first)` for the overloads in namespace `std`.
- `{last, result + (last - first)}` for the overloads in namespace `ranges`.

**11** *Complexity:* Exactly `last - first` applications of the corresponding predicate and any projection.
