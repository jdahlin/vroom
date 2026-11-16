# 23.4.3.2 `ranges::distance` [range.iter.op.distance]

[Contents](../README.md) · up: [23.4.3 Range iterator operations \[range.iter.ops\]](range.iter.ops.md) · prev: [23.4.3.1 `ranges::advance` \[range.iter.op.advance\]](range.iter.op.advance.md) · next: [23.4.3.3 `ranges::next` \[range.iter.op.next\]](range.iter.op.next.md)

```cpp
template<input_or_output_iterator I, sentinel_for<I> S>
  constexpr iter_difference_t<I> ranges::distance(I first, S last);
```

**1** *Preconditions:* `[first, last)` denotes a range, or `[last, first)` denotes a range and `S` and `I` model `same_as<S, I> && sized_sentinel_for<S, I>`.

**2** *Effects:* If `S` and `I` model `sized_sentinel_for<S, I>`, returns `(last - first)`; otherwise, returns the number of increments needed to get from `first` to `last`.

```cpp
template<range R>
  constexpr range_difference_t<R> ranges::distance(R&& r);
```

**3** *Effects:* If `R` models `sized_range`, equivalent to:

```cpp
return static_cast<range_difference_t<R>>(ranges::size(r));     // [range.prim.size]
```

Otherwise, equivalent to:

```cpp
return ranges::distance(ranges::begin(r), ranges::end(r));      // [range.access]
```
