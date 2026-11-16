# 8.7.4 The `co_return` statement [stmt.return.coroutine]

[Contents](README.md) · up: [8.7 Jump statements \[stmt.jump\]](stmt.jump.md) · prev: [8.7.3 The `return` statement \[stmt.return\]](stmt.return.md) · next: [8.7.5 The `goto` statement \[stmt.goto\]](stmt.goto.md)

```bnf
coroutine-return-statement:
    'co_return' expr-or-braced-init-listₒₚₜ ';'
```

**1** A coroutine returns to its caller or resumer ([[dcl.fct.def.coroutine]](dcl.fct.def.coroutine.md)) by the `co_return` statement or when suspended ([[expr.await]](expr.await.md)). A coroutine shall not enclose a `return` statement ([[stmt.return]](stmt.return.md)). [*Note 1*: For this determination, it is irrelevant whether the `return` statement is enclosed by a discarded statement ([[stmt.if]](stmt.if.md)). — *end note*]

**2** The *expr-or-braced-init-list* of a `co_return` statement is called its operand. Let *p* be an lvalue naming the coroutine promise object ([[dcl.fct.def.coroutine]](dcl.fct.def.coroutine.md)). A `co_return` statement is equivalent to:

```bnf
'{' S';' 'goto' final-suspend';' '}'
```

where `final-suspend` is the exposition-only label defined in [[dcl.fct.def.coroutine]](dcl.fct.def.coroutine.md) and *S* is defined as follows:

- If the operand is a *braced-init-list* or an expression of non-`void` type, *S* is *p*`.return_value(`*expr-or-braced-init-list*`)`. The expression *S* shall be a prvalue of type `void`.
- Otherwise, *S* is the *compound-statement* `{` *expression*ₒₚₜ `;` *p*`.return_void()``; }`. The expression *p*`.return_void()` shall be a prvalue of type `void`.

**3** If *p*`.return_void()` is a valid expression, flowing off the end of a coroutine is equivalent to a `co_return` with no operand; otherwise flowing off the end of a coroutine results in undefined behavior.
