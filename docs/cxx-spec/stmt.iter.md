# 8.6 Iteration statements [stmt.iter]

[Contents](README.md) · up: [8 Statements \[stmt.stmt\]](stmt.stmt.md) · prev: [8.5.2 The `switch` statement \[stmt.switch\]](stmt.switch.md) · next: [8.6.1 The `while` statement \[stmt.while\]](stmt.while.md)

**1** Iteration statements specify looping.

```bnf
iteration-statement:
    'while' '(' condition ')' statement
    'do' statement 'while' '(' expression ')' ';'
    'for' '(' init-statement conditionₒₚₜ ';' expressionₒₚₜ ')' statement
    'for' '(' init-statementₒₚₜ for-range-declaration ':' for-range-initializer ')' statement
```

```bnf
for-range-declaration:
    attribute-specifier-seqₒₚₜ decl-specifier-seq declarator
    attribute-specifier-seqₒₚₜ decl-specifier-seq ref-qualifierₒₚₜ '[' identifier-list ']'
```

```bnf
for-range-initializer:
    expr-or-braced-init-list
```

See [[dcl.meaning]](dcl.meaning.md) for the optional *attribute-specifier-seq* in a *for-range-declaration*. [*Note 1*: An *init-statement* ends with a semicolon. — *end note*]

**2** The substatement in an *iteration-statement* implicitly defines a block scope ([[basic.scope]](basic.scope.md)) which is entered and exited each time through the loop. If the substatement in an *iteration-statement* is a single statement and not a *compound-statement*, it is as if it was rewritten to be a *compound-statement* containing the original statement. [*Example 1*:

```cpp
while (--x >= 0)
  int i;
```

can be equivalently rewritten as

```cpp
while (--x >= 0) {
  int i;
}
```

Thus after the `while` statement, `i` is no longer in scope. — *end example*]

**3** If a name introduced in an *init-statement* or *for-range-declaration* is redeclared in the outermost block of the substatement, the program is ill-formed. [*Example 2*:

```cpp
void f() {
  for (int i = 0; i < 10; ++i)
    int i = 0;          // error: redeclaration
  for (int i : { 1, 2, 3 })
    int i = 1;          // error: redeclaration
}
```

— *end example*]

## Subclauses

- [8.6.1 The `while` statement \[stmt.while\]](stmt.while.md)
- [8.6.2 The `do` statement \[stmt.do\]](stmt.do.md)
- [8.6.3 The `for` statement \[stmt.for\]](stmt.for.md)
- [8.6.4 The range-based `for` statement \[stmt.ranged\]](stmt.ranged.md)
