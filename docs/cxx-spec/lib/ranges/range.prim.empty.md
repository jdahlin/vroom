# 24.3.11 `ranges::empty` [range.prim.empty]

[Contents](../../README.md) · up: [24.3 Range access \[range.access\]](range.access.md) · prev: [24.3.10 `ranges::ssize` \[range.prim.ssize\]](range.prim.ssize.md) · next: [24.3.12 `ranges::data` \[range.prim.data\]](range.prim.data.md)

**1** The name `ranges::empty` denotes a customization point object ([[customization.point.object]](../library/customization.point.object.md)).

**2** Given a subexpression `ranges::empty(E)` with type `T`, let `t` be an lvalue that denotes the reified object for `E`. Then:

- If `T` is an array of unknown bound ([[basic.compound]](../../basic/basic.compound.md)), `ranges::empty(E)` is ill-formed.
- Otherwise, if `bool(t.empty())` is a valid expression, `ranges::empty(E)` is expression-equivalent to `bool(t.empty())`.
- Otherwise, if `(ranges::size(t) == 0)` is a valid expression, `ranges::empty(E)` is expression-equivalent to `(ranges::size(t) == 0)`.
- Otherwise, if `bool(ranges::begin(t) == ranges::end(t))` is a valid expression and the type of `ranges::begin(t)` models `forward_iterator`, `ranges::empty(E)` is expression-equivalent to `bool(ranges::begin(t) == ranges::end(t))`.
- Otherwise, `ranges::empty(E)` is ill-formed.

**3** [*Note 1*: Diagnosable ill-formed cases above result in substitution failure when `ranges::empty(E)` appears in the immediate context of a template instantiation. — *end note*]

**4** [*Note 2*: Whenever `ranges::empty(E)` is a valid expression, it has type `bool`. — *end note*]
