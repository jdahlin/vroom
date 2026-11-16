# 24.3.12 `ranges::data` [range.prim.data]

[Contents](../README.md) · up: [24.3 Range access \[range.access\]](range.access.md) · prev: [24.3.11 `ranges::empty` \[range.prim.empty\]](range.prim.empty.md) · next: [24.3.13 `ranges::cdata` \[range.prim.cdata\]](range.prim.cdata.md)

**1** The name `ranges::data` denotes a customization point object ([[customization.point.object]](customization.point.object.md)).

**2** Given a subexpression `E` with type `T`, let `t` be an lvalue that denotes the reified object for `E`. Then:

- If `E` is an rvalue and `enable_borrowed_range<remove_cv_t<T>>` is `false`, `ranges::data(E)` is ill-formed.
- Otherwise, if `T` is an array type ([[basic.compound]](../basic.compound.md)) and `remove_all_extents_t<T>` is an incomplete type, `ranges::data(E)` is ill-formed with no diagnostic required.
- Otherwise, if `decay-copy(t.data())` is a valid expression of pointer to object type, `ranges::data(E)` is expression-equivalent to `decay-copy(t.data())`.
- Otherwise, if `ranges::begin(t)` is a valid expression whose type models `contiguous_iterator`, `ranges::data(E)` is expression-equivalent to `to_address(ranges::begin(E))`.
- Otherwise, `ranges::data(E)` is ill-formed.

**3** [*Note 1*: Diagnosable ill-formed cases above result in substitution failure when `ranges::data(E)` appears in the immediate context of a template instantiation. — *end note*]

**4** [*Note 2*: Whenever `ranges::data(E)` is a valid expression, it has pointer to object type. — *end note*]
