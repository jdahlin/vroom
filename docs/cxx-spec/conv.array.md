# 7.3.2 Array-to-pointer conversion [conv.array]

[Contents](README.md) · up: [7.3 Standard conversions \[conv\]](conv.md) · prev: [7.3.1 Lvalue-to-rvalue conversion \[conv.lval\]](conv.lval.md) · next: [7.3.3 Function-to-pointer conversion \[conv.func\]](conv.func.md)

**1** An lvalue or rvalue of type “array of `N` `T`” or “array of unknown bound of `T`” can be converted to a prvalue of type “pointer to `T`”. The temporary materialization conversion ([[conv.rval]](conv.rval.md)) is applied. The result is a pointer to the first element of the array.
