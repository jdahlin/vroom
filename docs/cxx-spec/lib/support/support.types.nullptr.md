# 17.2.3 Null pointers [support.types.nullptr]

[Contents](../../README.md) · up: [17.2 Common definitions \[support.types\]](support.types.md) · prev: [17.2.2 Header `<cstdlib>` synopsis \[cstdlib.syn\]](cstdlib.syn.md) · next: [17.2.4 Sizes, alignments, and offsets \[support.types.layout\]](support.types.layout.md)

**1** The type `nullptr_t` is a synonym for the type of a `nullptr` expression, and it has the characteristics described in [[basic.fundamental]](../../basic/basic.fundamental.md) and [[conv.ptr]](../../expr/conv.ptr.md). [*Note 1*: Although `nullptr`’s address cannot be taken, the address of another `nullptr_t` object that is an lvalue can be taken. — *end note*]

**2** The macro `NULL` is an implementation-defined null pointer constant.[^1]

*See also:* ISO C 7.19

[^1]: Possible definitions include `0` and `0L`, but not `(void*)0`.
