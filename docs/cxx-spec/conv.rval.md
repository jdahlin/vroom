# 7.3.4 Temporary materialization conversion [conv.rval]

[Contents](README.md) · up: [7.3 Standard conversions \[conv\]](conv.md) · prev: [7.3.3 Function-to-pointer conversion \[conv.func\]](conv.func.md) · next: [7.3.5 Qualification conversions \[conv.qual\]](conv.qual.md)

**1** A prvalue of type `T` can be converted to an xvalue of type `T`. This conversion initializes a temporary object ([[class.temporary]](class.temporary.md)) of type `T` from the prvalue by evaluating the prvalue with the temporary object as its result object, and produces an xvalue denoting the temporary object. `T` shall be a complete type. [*Note 1*: If `T` is a class type (or array thereof), it must have an accessible and non-deleted destructor; see [[class.dtor]](class.dtor.md). — *end note*] [*Example 1*:

```cpp
struct X { int n; };
int k = X().n;      // OK, X() prvalue is converted to xvalue
```

— *end example*]
