# 24.3.13 `ranges::cdata` [range.prim.cdata]

[Contents](../README.md) · up: [24.3 Range access \[range.access\]](range.access.md) · prev: [24.3.12 `ranges::data` \[range.prim.data\]](range.prim.data.md) · next: [24.4 Range requirements \[range.req\]](range.req.md)

**1** The name `ranges::cdata` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). The expression `ranges::cdata(E)` for a subexpression `E` of type `T` is expression-equivalent to:

- `ranges::data(static_cast<const T&>(E))` if `E` is an lvalue.
- Otherwise, `ranges::data(static_cast<const T&&>(E))`.

**2** [*Note 1*: Whenever `ranges::cdata(E)` is a valid expression, it has pointer to object type. — *end note*]
