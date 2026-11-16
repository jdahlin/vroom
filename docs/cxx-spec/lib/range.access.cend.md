# 24.3.4 `ranges::cend` [range.access.cend]

[Contents](../README.md) · up: [24.3 Range access \[range.access\]](range.access.md) · prev: [24.3.3 `ranges::cbegin` \[range.access.cbegin\]](range.access.cbegin.md) · next: [24.3.5 `ranges::rbegin` \[range.access.rbegin\]](range.access.rbegin.md)

**1** The name `ranges::cend` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). The expression `ranges::cend(E)` for a subexpression `E` of type `T` is expression-equivalent to:

- `ranges::end(static_cast<const T&>(E))` if `E` is an lvalue.
- Otherwise, `ranges::end(static_cast<const T&&>(E))`.

**2** [*Note 1*: Whenever `ranges::cend(E)` is a valid expression, the types `S` and `I` of `ranges::cend(E)` and `ranges::cbegin(E)` model `sentinel_for<S, I>`. — *end note*]
