# 7.6.2.2 Increment and decrement [expr.pre.incr]

[Contents](../README.md) · up: [7.6.2 Unary expressions \[expr.unary\]](expr.unary.md) · prev: [7.6.2.1 Unary operators \[expr.unary.op\]](expr.unary.op.md) · next: [7.6.2.3 Await \[expr.await\]](expr.await.md)

**1** The operand of prefix `++` is modified ([[defns.access]](../intro/intro.defs.md#defns.access)) by adding `1`. The operand shall be a modifiable lvalue. The type of the operand shall be an arithmetic type other than *cv* `bool`, or a pointer to a completely-defined object type. An operand with volatile-qualified type is deprecated; see [[depr.volatile.type]](../depr/depr.volatile.type.md). The result is the updated operand; it is an lvalue, and it is a bit-field if the operand is a bit-field. The expression `++x` is equivalent to `x+=1`. [*Note 1*: See the discussions of addition ([[expr.add]](expr.add.md)) and assignment operators ([[expr.ass]](expr.ass.md)) for information on conversions. — *end note*]

**2** The operand of prefix `--` is modified ([[defns.access]](../intro/intro.defs.md#defns.access)) by subtracting `1`. The requirements on the operand of prefix `--` and the properties of its result are otherwise the same as those of prefix `++`. [*Note 2*: For postfix increment and decrement, see [[expr.post.incr]](expr.post.incr.md). — *end note*]
