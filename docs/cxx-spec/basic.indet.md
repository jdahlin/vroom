# 6.7.4 Indeterminate values [basic.indet]

[Contents](README.md) · up: [6.7 Memory and objects \[basic.memobj\]](basic.memobj.md) · prev: [6.7.3 Lifetime \[basic.life\]](basic.life.md) · next: [6.7.5 Storage duration \[basic.stc\]](basic.stc.md)

**1** When storage for an object with automatic or dynamic storage duration is obtained, the object has an *indeterminate value*, and if no initialization is performed for the object, that object retains an indeterminate value until that value is replaced ([[expr.ass]](expr.ass.md)). [*Note 1*: Objects with static or thread storage duration are zero-initialized, see [[basic.start.static]](basic.start.static.md). — *end note*]

**2** If an indeterminate value is produced by an evaluation, the behavior is undefined except in the following cases:

- If an indeterminate value of unsigned ordinary character type ([[basic.fundamental]](basic.fundamental.md)) or `std::byte` type ([[cstddef.syn]](lib/cstddef.syn.md)) is produced by the evaluation of:

  - the second or third operand of a conditional expression ([[expr.cond]](expr.cond.md)),
  - the right operand of a comma expression ([[expr.comma]](expr.comma.md)),
  - the operand of a cast or conversion ([[conv.integral]](conv.integral.md), [[expr.type.conv]](expr.type.conv.md), [[expr.static.cast]](expr.static.cast.md), [[expr.cast]](expr.cast.md)) to an unsigned ordinary character type or `std::byte` type ([[cstddef.syn]](lib/cstddef.syn.md)), or
  - a discarded-value expression ([[expr.context]](expr.context.md)),

  then the result of the operation is an indeterminate value.

- If an indeterminate value of unsigned ordinary character type or `std::byte` type is produced by the evaluation of the right operand of a simple assignment operator ([[expr.ass]](expr.ass.md)) whose first operand is an lvalue of unsigned ordinary character type or `std::byte` type, an indeterminate value replaces the value of the object referred to by the left operand.

- If an indeterminate value of unsigned ordinary character type is produced by the evaluation of the initialization expression when initializing an object of unsigned ordinary character type, that object is initialized to an indeterminate value.

- If an indeterminate value of unsigned ordinary character type or `std::byte` type is produced by the evaluation of the initialization expression when initializing an object of `std::byte` type, that object is initialized to an indeterminate value.

[*Example 1*:

```cpp
int f(bool b) {
  unsigned char c;
  unsigned char d = c;          // OK, d has an indeterminate value
  int e = d;                    // undefined behavior
  return b ? d : 0;             // undefined behavior if b is true
}
```

— *end example*]
