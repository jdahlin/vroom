# 23.4.3.1 `ranges::advance` [range.iter.op.advance]

[Contents](../README.md) · up: [23.4.3 Range iterator operations \[range.iter.ops\]](range.iter.ops.md) · prev: [23.4.3 Range iterator operations \[range.iter.ops\]](range.iter.ops.md) · next: [23.4.3.2 `ranges::distance` \[range.iter.op.distance\]](range.iter.op.distance.md)

```cpp
template<input_or_output_iterator I>
  constexpr void ranges::advance(I& i, iter_difference_t<I> n);
```

**1** *Preconditions:* If `I` does not model `bidirectional_iterator`, `n` is not negative.

**2** *Effects:*

- If `I` models `random_access_iterator`, equivalent to `i += n`.
- Otherwise, if `n` is non-negative, increments `i` by `n`.
- Otherwise, decrements `i` by `-n`.

```cpp
template<input_or_output_iterator I, sentinel_for<I> S>
  constexpr void ranges::advance(I& i, S bound);
```

**3** *Preconditions:* `[i, bound)` denotes a range.

**4** *Effects:*

- If `I` and `S` model `assignable_from<I&, S>`, equivalent to `i = std::move(bound)`.
- Otherwise, if `S` and `I` model `sized_sentinel_for<S, I>`, equivalent to `ranges::advance(i, bound - i)`.
- Otherwise, while `bool(i != bound)` is `true`, increments `i`.

```cpp
template<input_or_output_iterator I, sentinel_for<I> S>
  constexpr iter_difference_t<I> ranges::advance(I& i, iter_difference_t<I> n, S bound);
```

**5** *Preconditions:* If `n > 0`, `[i, bound)` denotes a range. If `n == 0`, `[i, bound)` or `[bound, i)` denotes a range. If `n < 0`, `[bound, i)` denotes a range, `I` models `bidirectional_iterator`, and `I` and `S` model `same_as<I, S>`.

**6** *Effects:*

- If `S` and `I` model `sized_sentinel_for<S, I>`:

  - If |`n`| ≥ |`bound - i`|, equivalent to `ranges::advance(i, bound)`.
  - Otherwise, equivalent to `ranges::advance(i, n)`.

- Otherwise,

  - if `n` is non-negative, while `bool(i != bound)` is `true`, increments `i` but at most `n` times.
  - Otherwise, while `bool(i != bound)` is `true`, decrements `i` but at most `-n` times.

**7** *Returns:* `n - M`, where *M* is the difference between the ending and starting positions of `i`.
