# 7.3.3 Function-to-pointer conversion [conv.func]

[Contents](README.md) · up: [7.3 Standard conversions \[conv\]](conv.md) · prev: [7.3.2 Array-to-pointer conversion \[conv.array\]](conv.array.md) · next: [7.3.4 Temporary materialization conversion \[conv.rval\]](conv.rval.md)

**1** An lvalue of function type `T` can be converted to a prvalue of type “pointer to `T`”. The result is a pointer to the function.[^1]

[^1]: This conversion never applies to non-static member functions because an lvalue that refers to a non-static member function cannot be obtained.
