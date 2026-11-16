# 7.6.20 Comma operator [expr.comma]

[Contents](README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.19 Assignment and compound assignment operators \[expr.ass\]](expr.ass.md) · next: [7.7 Constant expressions \[expr.const\]](expr.const.md)

**1** The comma operator groups left-to-right.

```bnf
expression:
    assignment-expression
    expression ',' assignment-expression
```

A pair of expressions separated by a comma is evaluated left-to-right; the left expression is a discarded-value expression ([[expr.prop]](expr.prop.md)). The left expression is sequenced before the right expression ([[intro.execution]](intro.execution.md)). The type and value of the result are the type and value of the right operand; the result is of the same value category as its right operand, and is a bit-field if its right operand is a bit-field.

**2** In contexts where comma is given a special meaning, [*Example 1*: in lists of arguments to functions ([[expr.call]](expr.call.md)) and lists of initializers ([[dcl.init]](dcl.init.md)) — *end example*] the comma operator as described in this subclause can appear only in parentheses. [*Example 2*:

```cpp
f(a, (t=3, t+2), c);
```

has three arguments, the second of which has the value `5`. — *end example*]

**3** [*Note 1*: A comma expression appearing as the *expr-or-braced-init-list* of a subscripting expression ([[expr.sub]](expr.sub.md)) is deprecated; see [[depr.comma.subscript]](depr.comma.subscript.md). — *end note*]
