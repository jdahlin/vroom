# 25.6.4 For each [alg.foreach]

[Contents](../../README.md) · up: [25.6 Non-modifying sequence operations \[alg.nonmodifying\]](alg.nonmodifying.md) · prev: [25.6.3 None of \[alg.none.of\]](alg.none.of.md) · next: [25.6.5 Find \[alg.find\]](alg.find.md)

```cpp
template<class InputIterator, class Function>
  constexpr Function for_each(InputIterator first, InputIterator last, Function f);
```

**1** *Preconditions:* `Function` meets the *Cpp17MoveConstructible* requirements ([Table 28](../library/utility.arg.requirements.md#tab:cpp17.moveconstructible)). [*Note 1*: `Function` need not meet the requirements of *Cpp17CopyConstructible* ([Table 29](../library/utility.arg.requirements.md#tab:cpp17.copyconstructible)). — *end note*]

**2** *Effects:* Applies `f` to the result of dereferencing every iterator in the range `[first, last)`, starting from `first` and proceeding to `last - 1`. [*Note 2*: If the type of `first` meets the requirements of a mutable iterator, `f` may apply non-constant functions through the dereferenced iterator. — *end note*]

**3** *Returns:* `f`.

**4** *Complexity:* Applies `f` exactly `last - first` times.

**5** *Remarks:* If `f` returns a result, the result is ignored.

```cpp
template<class ExecutionPolicy, class ForwardIterator, class Function>
  void for_each(ExecutionPolicy&& exec,
                ForwardIterator first, ForwardIterator last,
                Function f);
```

**6** *Preconditions:* `Function` meets the *Cpp17CopyConstructible* requirements.

**7** *Effects:* Applies `f` to the result of dereferencing every iterator in the range `[first, last)`. [*Note 3*: If the type of `first` meets the requirements of a mutable iterator, `f` may apply non-constant functions through the dereferenced iterator. — *end note*]

**8** *Complexity:* Applies `f` exactly `last - first` times.

**9** *Remarks:* If `f` returns a result, the result is ignored. Implementations do not have the freedom granted under [[algorithms.parallel.exec]](algorithms.parallel.exec.md) to make arbitrary copies of elements from the input sequence.

**10** [*Note 4*: Does not return a copy of its `Function` parameter, since parallelization may not permit efficient state accumulation. — *end note*]

```cpp
template<input_iterator I, sentinel_for<I> S, class Proj = identity,
         indirectly_unary_invocable<projected<I, Proj>> Fun>
  constexpr ranges::for_each_result<I, Fun>
    ranges::for_each(I first, S last, Fun f, Proj proj = {});
template<input_range R, class Proj = identity,
         indirectly_unary_invocable<projected<iterator_t<R>, Proj>> Fun>
  constexpr ranges::for_each_result<borrowed_iterator_t<R>, Fun>
    ranges::for_each(R&& r, Fun f, Proj proj = {});
```

**11** *Effects:* Calls `invoke(f, invoke(proj, *i))` for every iterator `i` in the range `[first, last)`, starting from `first` and proceeding to `last - 1`. [*Note 5*: If the result of `invoke(proj, *i)` is a mutable reference, `f` may apply non-constant functions. — *end note*]

**12** *Returns:* `{last, std::move(f)}`.

**13** *Complexity:* Applies `f` and `proj` exactly `last - first` times.

**14** *Remarks:* If `f` returns a result, the result is ignored.

**15** [*Note 6*: The overloads in namespace `ranges` require `Fun` to model `copy_constructible`. — *end note*]

```cpp
template<class InputIterator, class Size, class Function>
  constexpr InputIterator for_each_n(InputIterator first, Size n, Function f);
```

**16** *Mandates:* The type `Size` is convertible to an integral type ([[conv.integral]](../../expr/conv.integral.md), [[class.conv]](../../class/class.conv.md)).

**17** *Preconditions:* `Function` meets the *Cpp17MoveConstructible* requirements. [*Note 7*: `Function` need not meet the requirements of *Cpp17CopyConstructible*. — *end note*]

**18** *Preconditions:* `n >= 0` is `true`.

**19** *Effects:* Applies `f` to the result of dereferencing every iterator in the range `[first, first + n)` in order. [*Note 8*: If the type of `first` meets the requirements of a mutable iterator, `f` may apply non-constant functions through the dereferenced iterator. — *end note*]

**20** *Returns:* `first + n`.

**21** *Remarks:* If `f` returns a result, the result is ignored.

```cpp
template<class ExecutionPolicy, class ForwardIterator, class Size, class Function>
  ForwardIterator for_each_n(ExecutionPolicy&& exec, ForwardIterator first, Size n,
                             Function f);
```

**22** *Mandates:* The type `Size` is convertible to an integral type ([[conv.integral]](../../expr/conv.integral.md), [[class.conv]](../../class/class.conv.md)).

**23** *Preconditions:* `Function` meets the *Cpp17CopyConstructible* requirements.

**24** *Preconditions:* `n >= 0` is `true`.

**25** *Effects:* Applies `f` to the result of dereferencing every iterator in the range `[first, first + n)`. [*Note 9*: If the type of `first` meets the requirements of a mutable iterator, `f` may apply non-constant functions through the dereferenced iterator. — *end note*]

**26** *Returns:* `first + n`.

**27** *Remarks:* If `f` returns a result, the result is ignored. Implementations do not have the freedom granted under [[algorithms.parallel.exec]](algorithms.parallel.exec.md) to make arbitrary copies of elements from the input sequence.

```cpp
template<input_iterator I, class Proj = identity,
         indirectly_unary_invocable<projected<I, Proj>> Fun>
  constexpr ranges::for_each_n_result<I, Fun>
    ranges::for_each_n(I first, iter_difference_t<I> n, Fun f, Proj proj = {});
```

**28** *Preconditions:* `n >= 0` is `true`.

**29** *Effects:* Calls `invoke(f, invoke(proj, *i))` for every iterator `i` in the range `[first, first + n)` in order. [*Note 10*: If the result of `invoke(proj, *i)` is a mutable reference, `f` may apply non-constant functions. — *end note*]

**30** *Returns:* `{first + n, std::move(f)}`.

**31** *Remarks:* If `f` returns a result, the result is ignored.

**32** [*Note 11*: The overload in namespace `ranges` requires `Fun` to model `copy_constructible`. — *end note*]
