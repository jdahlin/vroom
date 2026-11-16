# 7.6.1 Postfix expressions [expr.post]

[Contents](README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · next: [7.6.1.1 Subscripting \[expr.sub\]](expr.sub.md)

**1** Postfix expressions group left-to-right.

```bnf
postfix-expression:
    primary-expression
    postfix-expression '[' expr-or-braced-init-list ']'
    postfix-expression '(' expression-listₒₚₜ ')'
    simple-type-specifier '(' expression-listₒₚₜ ')'
    typename-specifier '(' expression-listₒₚₜ ')'
    simple-type-specifier braced-init-list
    typename-specifier braced-init-list
    postfix-expression '.' 'template'ₒₚₜ id-expression
    postfix-expression '->' 'template'ₒₚₜ id-expression
    postfix-expression '++'
    postfix-expression '--'
    'dynamic_cast' '<' type-id '>' '(' expression ')'
    'static_cast' '<' type-id '>' '(' expression ')'
    'reinterpret_cast' '<' type-id '>' '(' expression ')'
    'const_cast' '<' type-id '>' '(' expression ')'
    'typeid' '(' expression ')'
    'typeid' '(' type-id ')'
```

```bnf
expression-list:
    initializer-list
```

**2** [*Note 1*: The `>` token following the *type-id* in a `dynamic_cast`, `static_cast`, `reinterpret_cast`, or `const_cast` may be the product of replacing a `>>` token by two consecutive `>` tokens ([[temp.names]](temp.names.md)). — *end note*]

## Subclauses

- [7.6.1.1 Subscripting \[expr.sub\]](expr.sub.md)
- [7.6.1.2 Function call \[expr.call\]](expr.call.md)
- [7.6.1.3 Explicit type conversion (functional notation) \[expr.type.conv\]](expr.type.conv.md)
- [7.6.1.4 Class member access \[expr.ref\]](expr.ref.md)
- [7.6.1.5 Increment and decrement \[expr.post.incr\]](expr.post.incr.md)
- [7.6.1.6 Dynamic cast \[expr.dynamic.cast\]](expr.dynamic.cast.md)
- [7.6.1.7 Type identification \[expr.typeid\]](expr.typeid.md)
- [7.6.1.8 Static cast \[expr.static.cast\]](expr.static.cast.md)
- [7.6.1.9 Reinterpret cast \[expr.reinterpret.cast\]](expr.reinterpret.cast.md)
- [7.6.1.10 Const cast \[expr.const.cast\]](expr.const.cast.md)
