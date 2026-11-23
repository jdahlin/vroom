# 24.3.3 `ranges::cbegin` [range.access.cbegin]

[Contents](../../README.md) · up: [24.3 Range access \[range.access\]](range.access.md) · prev: [24.3.2 `ranges::end` \[range.access.end\]](range.access.end.md) · next: [24.3.4 `ranges::cend` \[range.access.cend\]](range.access.cend.md)

**1** The name `ranges::cbegin` denotes a customization point object ([[customization.point.object]](../library/customization.point.object.md)). The expression `ranges::cbegin(E)` for a subexpression `E` of type `T` is expression-equivalent to:

- `ranges::begin(static_cast<const T&>(E))` if `E` is an lvalue.
- Otherwise, `ranges::begin(static_cast<const T&&>(E))`.

**2** [*Note 1*: Whenever `ranges::cbegin(E)` is a valid expression, its type models `input_or_output_iterator`. — *end note*]
