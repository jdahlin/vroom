# 24.3.10 `ranges::ssize` [range.prim.ssize]

[Contents](../../README.md) · up: [24.3 Range access \[range.access\]](range.access.md) · prev: [24.3.9 `ranges::size` \[range.prim.size\]](range.prim.size.md) · next: [24.3.11 `ranges::empty` \[range.prim.empty\]](range.prim.empty.md)

**1** The name `ranges::ssize` denotes a customization point object ([[customization.point.object]](../library/customization.point.object.md)). The expression `ranges::ssize(E)` for a subexpression `E` of type `T` is expression-equivalent to:

- If `range_difference_t<T>` has width less than `ptrdiff_t`, `static_cast<ptrdiff_t>(ranges::size(E))`.
- Otherwise, `static_cast<range_difference_t<T>>(ranges::size(E))`.
