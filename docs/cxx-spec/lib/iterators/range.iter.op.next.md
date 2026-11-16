# 23.4.3.3 `ranges::next` [range.iter.op.next]

[Contents](../../README.md) · up: [23.4.3 Range iterator operations \[range.iter.ops\]](range.iter.ops.md) · prev: [23.4.3.2 `ranges::distance` \[range.iter.op.distance\]](range.iter.op.distance.md) · next: [23.4.3.4 `ranges::prev` \[range.iter.op.prev\]](range.iter.op.prev.md)

```cpp
template<input_or_output_iterator I>
  constexpr I ranges::next(I x);
```

**1** *Effects:* Equivalent to: `++x; return x;`

```cpp
template<input_or_output_iterator I>
  constexpr I ranges::next(I x, iter_difference_t<I> n);
```

**2** *Effects:* Equivalent to: `ranges::advance(x, n); return x;`

```cpp
template<input_or_output_iterator I, sentinel_for<I> S>
  constexpr I ranges::next(I x, S bound);
```

**3** *Effects:* Equivalent to: `ranges::advance(x, bound); return x;`

```cpp
template<input_or_output_iterator I, sentinel_for<I> S>
  constexpr I ranges::next(I x, iter_difference_t<I> n, S bound);
```

**4** *Effects:* Equivalent to: `ranges::advance(x, n, bound); return x;`
