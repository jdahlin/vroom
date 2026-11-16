# 24.6.1.1 Overview [range.empty.overview]

[Contents](../README.md) · up: [24.6.1 Empty view \[range.empty\]](range.empty.md) · prev: [24.6.1 Empty view \[range.empty\]](range.empty.md) · next: [24.6.1.2 Class template `empty_view` \[range.empty.view\]](range.empty.view.md)

**1** `empty_view` produces a `view` of no elements of a particular type.

**2** [*Example 1*:

```cpp
empty_view<int> e;
static_assert(ranges::empty(e));
static_assert(0 == e.size());
```

— *end example*]
