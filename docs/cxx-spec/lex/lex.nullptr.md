# 5.13.7 Pointer literals [lex.nullptr]

[Contents](../README.md) · up: [5.13 Literals \[lex.literal\]](lex.literal.md) · prev: [5.13.6 Boolean literals \[lex.bool\]](lex.bool.md) · next: [5.13.8 User-defined literals \[lex.ext\]](lex.ext.md)

```bnf
pointer-literal:
    'nullptr'
```

**1** The pointer literal is the keyword `nullptr`. It is a prvalue of type `std::nullptr_t`. [*Note 1*: `std::nullptr_t` is a distinct type that is neither a pointer type nor a pointer-to-member type; rather, a prvalue of this type is a null pointer constant and can be converted to a null pointer value or null member pointer value. See [[conv.ptr]](../expr/conv.ptr.md) and [[conv.mem]](../expr/conv.mem.md). — *end note*]
