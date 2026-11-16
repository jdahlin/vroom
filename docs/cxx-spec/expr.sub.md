# 7.6.1.1 Subscripting [expr.sub]

[Contents](README.md) · up: [7.6.1 Postfix expressions \[expr.post\]](expr.post.md) · prev: [7.6.1 Postfix expressions \[expr.post\]](expr.post.md) · next: [7.6.1.2 Function call \[expr.call\]](expr.call.md)

**1** A postfix expression followed by an expression in square brackets is a postfix expression. One of the expressions shall be a glvalue of type “array of `T`” or a prvalue of type “pointer to `T`” and the other shall be a prvalue of unscoped enumeration or integral type. The result is of type “`T`”. The type “`T`” shall be a completely-defined object type.[^1] The expression `E1[E2]` is identical (by definition) to `*((E1)+(E2))`, except that in the case of an array operand, the result is an lvalue if that operand is an lvalue and an xvalue otherwise. The expression `E1` is sequenced before the expression `E2`.

**2** [*Note 1*: A comma expression ([[expr.comma]](expr.comma.md)) appearing as the *expr-or-braced-init-list* of a subscripting expression is deprecated; see [[depr.comma.subscript]](depr.comma.subscript.md). — *end note*]

**3** [*Note 2*: Despite its asymmetric appearance, subscripting is a commutative operation except for sequencing. See [[expr.unary]](expr.unary.md) and [[expr.add]](expr.add.md) for details of `*` and `+` and [[dcl.array]](dcl.array.md) for details of array types. — *end note*]

**4** A *braced-init-list* shall not be used with the built-in subscript operator.

[^1]: This is true even if the subscript operator is used in the following common idiom: `&x[0]`.
