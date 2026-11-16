# 25.6.2 Any of [alg.any.of]

[Contents](../../README.md) · up: [25.6 Non-modifying sequence operations \[alg.nonmodifying\]](alg.nonmodifying.md) · prev: [25.6.1 All of \[alg.all.of\]](alg.all.of.md) · next: [25.6.3 None of \[alg.none.of\]](alg.none.of.md)

```cpp
template<class InputIterator, class Predicate>
  constexpr bool any_of(InputIterator first, InputIterator last, Predicate pred);
template<class ExecutionPolicy, class ForwardIterator, class Predicate>
  bool any_of(ExecutionPolicy&& exec, ForwardIterator first, ForwardIterator last,
              Predicate pred);

template<input_iterator I, sentinel_for<I> S, class Proj = identity,
         indirect_unary_predicate<projected<I, Proj>> Pred>
  constexpr bool ranges::any_of(I first, S last, Pred pred, Proj proj = {});
template<input_range R, class Proj = identity,
         indirect_unary_predicate<projected<iterator_t<R>, Proj>> Pred>
  constexpr bool ranges::any_of(R&& r, Pred pred, Proj proj = {});
```

**1** Let *E* be:

- `pred(*i)` for the overloads in namespace `std`;
- `invoke(pred, invoke(proj, *i))` for the overloads in namespace `ranges`.

**2** *Returns:* `true` if *E* is `true` for some iterator `i` in the range `[first, last)`, and `false` otherwise.

**3** *Complexity:* At most `last - first` applications of the predicate and any projection.
