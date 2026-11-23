# 8.4 Compound statement or block [stmt.block]

[Contents](../README.md) · up: [8 Statements \[stmt.stmt\]](stmt.stmt.md) · prev: [8.3 Expression statement \[stmt.expr\]](stmt.expr.md) · next: [8.5 Selection statements \[stmt.select\]](stmt.select.md)

**1** So that several statements can be used where one is expected, the compound statement (also, and equivalently, called “block”) is provided.

```bnf
compound-statement:
    '{' statement-seqₒₚₜ '}'
```

```bnf
statement-seq:
    statement
    statement-seq statement
```

A compound statement defines a block scope ([[basic.scope]](../basic/basic.scope.md)). [*Note 1*: A declaration is a *statement* ([[stmt.dcl]](stmt.dcl.md)). — *end note*]
