# 24.7.3 All view [range.all]

[Contents](../README.md) · up: [24.7 Range adaptors \[range.adaptors\]](range.adaptors.md) · prev: [24.7.2 Semiregular wrapper \[range.semi.wrap\]](range.semi.wrap.md) · next: [24.7.3.1 Class template `ref_view` \[range.ref.view\]](range.ref.view.md)

**1** `views::all` returns a `view` that includes all elements of its `range` argument.

**2** The name `views::all` denotes a range adaptor object ([[range.adaptor.object]](range.adaptor.object.md)). Given a subexpression `E`, the expression `views::all(E)` is expression-equivalent to:

- `decay-copy(E)` if the decayed type of `E` models `view`.
- Otherwise, `ref_view{E}` if that expression is well-formed.
- Otherwise, `subrange{E}`.

## Subclauses

- [24.7.3.1 Class template `ref_view` \[range.ref.view\]](range.ref.view.md)
