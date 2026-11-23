# 8.6.2 The `do` statement [stmt.do]

[Contents](../README.md) · up: [8.6 Iteration statements \[stmt.iter\]](stmt.iter.md) · prev: [8.6.1 The `while` statement \[stmt.while\]](stmt.while.md) · next: [8.6.3 The `for` statement \[stmt.for\]](stmt.for.md)

**1** The expression is contextually converted to `bool` ([[conv]](../expr/conv.md)); if that conversion is ill-formed, the program is ill-formed.

**2** In the `do` statement the substatement is executed repeatedly until the value of the expression becomes `false`. The test takes place after each execution of the statement.
