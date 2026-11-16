# 24.3.7 `ranges::crbegin` [range.access.crbegin]

[Contents](../README.md) · up: [24.3 Range access \[range.access\]](range.access.md) · prev: [24.3.6 `ranges::rend` \[range.access.rend\]](range.access.rend.md) · next: [24.3.8 `ranges::crend` \[range.access.crend\]](range.access.crend.md)

**1** The name `ranges::crbegin` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). The expression `ranges::crbegin(E)` for a subexpression `E` of type `T` is expression-equivalent to:

- `ranges::rbegin(static_cast<const T&>(E))` if `E` is an lvalue.
- Otherwise, `ranges::rbegin(static_cast<const T&&>(E))`.

**2** [*Note 1*: Whenever `ranges::crbegin(E)` is a valid expression, its type models `input_or_output_iterator`. — *end note*]
