# 8.7 Jump statements [stmt.jump]

[Contents](README.md) · up: [8 Statements \[stmt.stmt\]](stmt.stmt.md) · prev: [8.6.4 The range-based `for` statement \[stmt.ranged\]](stmt.ranged.md) · next: [8.7.1 The `break` statement \[stmt.break\]](stmt.break.md)

**1** Jump statements unconditionally transfer control.

```bnf
jump-statement:
    'break' ';'
    'continue' ';'
    'return' expr-or-braced-init-listₒₚₜ ';'
    coroutine-return-statement
    'goto' identifier ';'
```

**2** On exit from a scope (however accomplished), objects with automatic storage duration ([[basic.stc.auto]](basic.stc.auto.md)) that have been constructed in that scope are destroyed in the reverse order of their construction. [*Note 1*: For temporaries, see [[class.temporary]](class.temporary.md). — *end note*] Transfer out of a loop, out of a block, or back past an initialized variable with automatic storage duration involves the destruction of objects with automatic storage duration that are in scope at the point transferred from but not at the point transferred to. (See [[stmt.dcl]](stmt.dcl.md) for transfers into blocks). [*Note 2*: However, the program can be terminated (by calling `std::exit()` or `std::abort()` ([[support.start.term]](lib/support.start.term.md)), for example) without destroying objects with automatic storage duration. — *end note*] [*Note 3*: A suspension of a coroutine ([[expr.await]](expr.await.md)) is not considered to be an exit from a scope. — *end note*]

## Subclauses

- [8.7.1 The `break` statement \[stmt.break\]](stmt.break.md)
- [8.7.2 The `continue` statement \[stmt.cont\]](stmt.cont.md)
- [8.7.3 The `return` statement \[stmt.return\]](stmt.return.md)
- [8.7.4 The `co_return` statement \[stmt.return.coroutine\]](stmt.return.coroutine.md)
- [8.7.5 The `goto` statement \[stmt.goto\]](stmt.goto.md)
