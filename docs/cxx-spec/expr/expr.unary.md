# 7.6.2 Unary expressions [expr.unary]

[Contents](../README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.1.10 Const cast \[expr.const.cast\]](expr.const.cast.md) · next: [7.6.2.1 Unary operators \[expr.unary.op\]](expr.unary.op.md)

**1** Expressions with unary operators group right-to-left.

```bnf
unary-expression:
    postfix-expression
    unary-operator cast-expression
    '++' cast-expression
    '--' cast-expression
    await-expression
    'sizeof' unary-expression
    'sizeof' '(' type-id ')'
    'sizeof' '...' '(' identifier ')'
    'alignof' '(' type-id ')'
    noexcept-expression
    new-expression
    delete-expression
```

```bnf
unary-operator: one of
    '* & + - ! ~'
```

## Subclauses

- [7.6.2.1 Unary operators \[expr.unary.op\]](expr.unary.op.md)
- [7.6.2.2 Increment and decrement \[expr.pre.incr\]](expr.pre.incr.md)
- [7.6.2.3 Await \[expr.await\]](expr.await.md)
- [7.6.2.4 Sizeof \[expr.sizeof\]](expr.sizeof.md)
- [7.6.2.5 Alignof \[expr.alignof\]](expr.alignof.md)
- [7.6.2.6 `noexcept` operator \[expr.unary.noexcept\]](expr.unary.noexcept.md)
- [7.6.2.7 New \[expr.new\]](expr.new.md)
- [7.6.2.8 Delete \[expr.delete\]](expr.delete.md)
