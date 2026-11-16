# 24.3.8 `ranges::crend` [range.access.crend]

[Contents](../../README.md) · up: [24.3 Range access \[range.access\]](range.access.md) · prev: [24.3.7 `ranges::crbegin` \[range.access.crbegin\]](range.access.crbegin.md) · next: [24.3.9 `ranges::size` \[range.prim.size\]](range.prim.size.md)

**1** The name `ranges::crend` denotes a customization point object ([[customization.point.object]](../library/customization.point.object.md)). The expression `ranges::crend(E)` for a subexpression `E` of type `T` is expression-equivalent to:

- `ranges::rend(static_cast<const T&>(E))` if `E` is an lvalue.
- Otherwise, `ranges::rend(static_cast<const T&&>(E))`.

**2** [*Note 1*: Whenever `ranges::crend(E)` is a valid expression, the types `S` and `I` of `ranges::crend(E)` and `ranges::crbegin(E)` model `sentinel_for<S, I>`. — *end note*]
