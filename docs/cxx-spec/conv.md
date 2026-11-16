# 7.3 Standard conversions [conv]

[Contents](README.md) · up: [7 Expressions \[expr\]](expr.md) · prev: [7.2.3 Context dependence \[expr.context\]](expr.context.md) · next: [7.3.1 Lvalue-to-rvalue conversion \[conv.lval\]](conv.lval.md)

**1** Standard conversions are implicit conversions with built-in meaning. [[conv]](conv.md) enumerates the full set of such conversions. A *standard conversion sequence* is a sequence of standard conversions in the following order:

- Zero or one conversion from the following set: lvalue-to-rvalue conversion, array-to-pointer conversion, and function-to-pointer conversion.
- Zero or one conversion from the following set: integral promotions, floating-point promotion, integral conversions, floating-point conversions, floating-integral conversions, pointer conversions, pointer-to-member conversions, and boolean conversions.
- Zero or one function pointer conversion.
- Zero or one qualification conversion.

[*Note 1*: A standard conversion sequence can be empty, i.e., it can consist of no conversions. — *end note*] A standard conversion sequence will be applied to an expression if necessary to convert it to a required destination type.

**2** [*Note 2*: Expressions with a given type will be implicitly converted to other types in several contexts:

- When used as operands of operators. The operator’s requirements for its operands dictate the destination type ([[expr.compound]](expr.compound.md)).
- When used in the condition of an `if` statement ([[stmt.if]](stmt.if.md)) or iteration statement ([[stmt.iter]](stmt.iter.md)). The destination type is `bool`.
- When used in the expression of a `switch` statement ([[stmt.switch]](stmt.switch.md)). The destination type is integral.
- When used as the source expression for an initialization (which includes use as an argument in a function call and use as the expression in a `return` statement). The type of the entity being initialized is (generally) the destination type. See [[dcl.init]](dcl.init.md), [[dcl.init.ref]](dcl.init.ref.md).

— *end note*]

**3** An expression *E* can be *implicitly converted* to a type `T` if and only if the declaration `T t=E;` is well-formed, for some invented temporary variable `t` ([[dcl.init]](dcl.init.md)).

**4** Certain language constructs require that an expression be converted to a Boolean value. An expression *E* appearing in such a context is said to be *contextually converted to `bool`* and is well-formed if and only if the declaration `bool t(E);` is well-formed, for some invented temporary variable `t` ([[dcl.init]](dcl.init.md)).

**5** Certain language constructs require conversion to a value having one of a specified set of types appropriate to the construct. An expression *E* of class type `C` appearing in such a context is said to be *contextually implicitly converted* to a specified type `T` and is well-formed if and only if *E* can be implicitly converted to a type `T` that is determined as follows: `C` is searched for non-explicit conversion functions whose return type is *cv* `T` or reference to *cv* `T` such that `T` is allowed by the context. There shall be exactly one such `T`.

**6** The effect of any implicit conversion is the same as performing the corresponding declaration and initialization and then using the temporary variable as the result of the conversion. The result is an lvalue if `T` is an lvalue reference type or an rvalue reference to function type ([[dcl.ref]](dcl.ref.md)), an xvalue if `T` is an rvalue reference to object type, and a prvalue otherwise. The expression *E* is used as a glvalue if and only if the initialization uses it as a glvalue.

**7** [*Note 3*: For class types, user-defined conversions are considered as well; see [[class.conv]](class.conv.md). In general, an implicit conversion sequence ([[over.best.ics]](over.best.ics.md)) consists of a standard conversion sequence followed by a user-defined conversion followed by another standard conversion sequence. — *end note*]

**8** [*Note 4*: There are some contexts where certain conversions are suppressed. For example, the lvalue-to-rvalue conversion is not done on the operand of the unary `&` operator. Specific exceptions are given in the descriptions of those operators and contexts. — *end note*]

## Subclauses

- [7.3.1 Lvalue-to-rvalue conversion \[conv.lval\]](conv.lval.md)
- [7.3.2 Array-to-pointer conversion \[conv.array\]](conv.array.md)
- [7.3.3 Function-to-pointer conversion \[conv.func\]](conv.func.md)
- [7.3.4 Temporary materialization conversion \[conv.rval\]](conv.rval.md)
- [7.3.5 Qualification conversions \[conv.qual\]](conv.qual.md)
- [7.3.6 Integral promotions \[conv.prom\]](conv.prom.md)
- [7.3.7 Floating-point promotion \[conv.fpprom\]](conv.fpprom.md)
- [7.3.8 Integral conversions \[conv.integral\]](conv.integral.md)
- [7.3.9 Floating-point conversions \[conv.double\]](conv.double.md)
- [7.3.10 Floating-integral conversions \[conv.fpint\]](conv.fpint.md)
- [7.3.11 Pointer conversions \[conv.ptr\]](conv.ptr.md)
- [7.3.12 Pointer-to-member conversions \[conv.mem\]](conv.mem.md)
- [7.3.13 Function pointer conversions \[conv.fctptr\]](conv.fctptr.md)
- [7.3.14 Boolean conversions \[conv.bool\]](conv.bool.md)
