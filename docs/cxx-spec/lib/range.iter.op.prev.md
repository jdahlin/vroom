# 23.4.3.4 `ranges::prev` [range.iter.op.prev]

[Contents](../README.md) · up: [23.4.3 Range iterator operations \[range.iter.ops\]](range.iter.ops.md) · prev: [23.4.3.3 `ranges::next` \[range.iter.op.next\]](range.iter.op.next.md) · next: [23.5 Iterator adaptors \[predef.iterators\]](predef.iterators.md)

```cpp
template<bidirectional_iterator I>
  constexpr I ranges::prev(I x);
```

**1** *Effects:* Equivalent to: `--x; return x;`

```cpp
template<bidirectional_iterator I>
  constexpr I ranges::prev(I x, iter_difference_t<I> n);
```

**2** *Effects:* Equivalent to: `ranges::advance(x, -n); return x;`

```cpp
template<bidirectional_iterator I>
  constexpr I ranges::prev(I x, iter_difference_t<I> n, I bound);
```

**3** *Effects:* Equivalent to: `ranges::advance(x, -n, bound); return x;`
