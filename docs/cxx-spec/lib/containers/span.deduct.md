# 22.7.3.3 Deduction guides [span.deduct]

[Contents](../../README.md) · up: [22.7.3 Class template `span` \[views.span\]](views.span.md) · prev: [22.7.3.2 Constructors, copy, and assignment \[span.cons\]](span.cons.md) · next: [22.7.3.4 Subviews \[span.sub\]](span.sub.md)

```cpp
template<class It, class EndOrSize>
  span(It, EndOrSize) -> span<remove_reference_t<iter_reference_t<It>>>;
```

**1** *Constraints:* `It` satisfies `contiguous_iterator`.

```cpp
template<class R>
  span(R&&) -> span<remove_reference_t<ranges::range_reference_t<R>>>;
```

**2** *Constraints:* `R` satisfies `ranges::contiguous_range`.
