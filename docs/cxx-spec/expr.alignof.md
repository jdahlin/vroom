# 7.6.2.5 Alignof [expr.alignof]

[Contents](README.md) · up: [7.6.2 Unary expressions \[expr.unary\]](expr.unary.md) · prev: [7.6.2.4 Sizeof \[expr.sizeof\]](expr.sizeof.md) · next: [7.6.2.6 `noexcept` operator \[expr.unary.noexcept\]](expr.unary.noexcept.md)

**1** An `alignof` expression yields the alignment requirement of its operand type. The operand shall be a *type-id* representing a complete object type, or an array thereof, or a reference to one of those types.

**2** The result is a prvalue of type `std::size_t`. [*Note 1*: An `alignof` expression is an integral constant expression ([[expr.const]](expr.const.md)). The type `std::size_t` is defined in the standard header `<cstddef>` ([[cstddef.syn]](lib/cstddef.syn.md), [[support.types.layout]](lib/support.types.layout.md)). — *end note*]

**3** When `alignof` is applied to a reference type, the result is the alignment of the referenced type. When `alignof` is applied to an array type, the result is the alignment of the element type.
