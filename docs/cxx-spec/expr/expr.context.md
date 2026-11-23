# 7.2.3 Context dependence [expr.context]

[Contents](../README.md) · up: [7.2 Properties of expressions \[expr.prop\]](expr.prop.md) · prev: [7.2.2 Type \[expr.type\]](expr.type.md) · next: [7.3 Standard conversions \[conv\]](conv.md)

**1** In some contexts, *unevaluated operands* appear ([[expr.prim.req]](expr.prim.req.md), [[expr.typeid]](expr.typeid.md), [[expr.sizeof]](expr.sizeof.md), [[expr.unary.noexcept]](expr.unary.noexcept.md), [[dcl.type.simple]](../dcl/dcl.type.simple.md), [[temp.pre]](../temp/temp.pre.md), [[temp.concept]](../temp/temp.concept.md)). An unevaluated operand is not evaluated. [*Note 1*: In an unevaluated operand, a non-static class member may be named ([[expr.prim.id]](expr.prim.id.md)) and naming of objects or functions does not, by itself, require that a definition be provided ([[basic.def.odr]](../basic/basic.def.odr.md)). An unevaluated operand is considered a full-expression ([[intro.execution]](../basic/intro.execution.md)). — *end note*]

**2** In some contexts, an expression only appears for its side effects. Such an expression is called a *discarded-value expression*. The array-to-pointer ([[conv.array]](conv.array.md)) and function-to-pointer ([[conv.func]](conv.func.md)) standard conversions are not applied. The lvalue-to-rvalue conversion ([[conv.lval]](conv.lval.md)) is applied if and only if the expression is a glvalue of volatile-qualified type and it is one of the following:

- `(` *expression* `)`, where *expression* is one of these expressions,
- *id-expression* ([[expr.prim.id]](expr.prim.id.md)),
- subscripting ([[expr.sub]](expr.sub.md)),
- class member access ([[expr.ref]](expr.ref.md)),
- indirection ([[expr.unary.op]](expr.unary.op.md)),
- pointer-to-member operation ([[expr.mptr.oper]](expr.mptr.oper.md)),
- conditional expression ([[expr.cond]](expr.cond.md)) where both the second and the third operands are one of these expressions, or
- comma expression ([[expr.comma]](expr.comma.md)) where the right operand is one of these expressions.

[*Note 2*: Using an overloaded operator causes a function call; the above covers only operators with built-in meaning. — *end note*] If the (possibly converted) expression is a prvalue, the temporary materialization conversion ([[conv.rval]](conv.rval.md)) is applied. [*Note 3*: If the expression is an lvalue of class type, it must have a volatile copy constructor to initialize the temporary object that is the result object of the lvalue-to-rvalue conversion. — *end note*] The glvalue expression is evaluated and its value is discarded.
