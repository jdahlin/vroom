# 8.5 Selection statements [stmt.select]

[Contents](../README.md) · up: [8 Statements \[stmt.stmt\]](stmt.stmt.md) · prev: [8.4 Compound statement or block \[stmt.block\]](stmt.block.md) · next: [8.5.1 The `if` statement \[stmt.if\]](stmt.if.md)

**1** Selection statements choose one of several flows of control.

```bnf
selection-statement:
    'if' 'constexpr'ₒₚₜ '(' init-statementₒₚₜ condition ')' statement
    'if' 'constexpr'ₒₚₜ '(' init-statementₒₚₜ condition ')' statement 'else' statement
    'switch' '(' init-statementₒₚₜ condition ')' statement
```

See [[dcl.meaning]](../dcl/dcl.meaning.md) for the optional *attribute-specifier-seq* in a condition. [*Note 1*: An *init-statement* ends with a semicolon. — *end note*]

**2** The substatement in a *selection-statement* (each substatement, in the `else` form of the `if` statement) implicitly defines a block scope ([[basic.scope]](../basic/basic.scope.md)). If the substatement in a *selection-statement* is a single statement and not a *compound-statement*, it is as if it was rewritten to be a *compound-statement* containing the original substatement. [*Example 1*:

```cpp
if (x)
  int i;
```

can be equivalently rewritten as

```cpp
if (x) {
  int i;
}
```

Thus after the `if` statement, `i` is no longer in scope. — *end example*]

## Subclauses

- [8.5.1 The `if` statement \[stmt.if\]](stmt.if.md)
- [8.5.2 The `switch` statement \[stmt.switch\]](stmt.switch.md)
