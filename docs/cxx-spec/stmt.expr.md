# 8.3 Expression statement [stmt.expr]

[Contents](README.md) · up: [8 Statements \[stmt.stmt\]](stmt.stmt.md) · prev: [8.2 Labeled statement \[stmt.label\]](stmt.label.md) · next: [8.4 Compound statement or block \[stmt.block\]](stmt.block.md)

**1** Expression statements have the form

```bnf
expression-statement:
    expressionₒₚₜ ';'
```

The expression is a discarded-value expression ([[expr.context]](expr.context.md)). All side effects from an expression statement are completed before the next statement is executed. An expression statement with the expression missing is called a *null statement*. [*Note 1*: Most statements are expression statements — usually assignments or function calls. A null statement is useful to carry a label just before the `}` of a compound statement and to supply a null body to an iteration statement such as a `while` statement ([[stmt.while]](stmt.while.md)). — *end note*]
