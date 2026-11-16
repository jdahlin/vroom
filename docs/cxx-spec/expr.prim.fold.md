# 7.5.6 Fold expressions [expr.prim.fold]

[Contents](README.md) · up: [7.5 Primary expressions \[expr.prim\]](expr.prim.md) · prev: [7.5.5.2 Captures \[expr.prim.lambda.capture\]](expr.prim.lambda.capture.md) · next: [7.5.7 Requires expressions \[expr.prim.req\]](expr.prim.req.md)

**1** A fold expression performs a fold of a pack ([[temp.variadic]](temp.variadic.md)) over a binary operator.

```bnf
fold-expression:
    '(' cast-expression fold-operator '...' ')'
    '(' '...' fold-operator cast-expression ')'
    '(' cast-expression fold-operator '...' fold-operator cast-expression ')'
```

```bnf
fold-operator: one of
    '+' '-' '*' '/' '%' '^' '&' '|' '<<' '>>'
    '+=' '-=' '*=' '/=' '%=' '^=' '&=' '|=' '<<=' '>>=' '='
    '==' '!=' '<' '>' '<=' '>=' '&&' '||' ',' '.*' '->*'
```

**2** An expression of the form `(...` *op* `e)` where *op* is a *fold-operator* is called a *unary left fold*. An expression of the form `(e` *op* `...)` where *op* is a *fold-operator* is called a *unary right fold*. Unary left folds and unary right folds are collectively called *unary folds*. In a unary fold, the *cast-expression* shall contain an unexpanded pack ([[temp.variadic]](temp.variadic.md)).

**3** An expression of the form `(e1` *op1* `...` *op2* `e2)` where *op1* and *op2* are *fold-operator*s is called a *binary fold*. In a binary fold, *op1* and *op2* shall be the same *fold-operator*, and either `e1` shall contain an unexpanded pack or `e2` shall contain an unexpanded pack, but not both. If `e2` contains an unexpanded pack, the expression is called a *binary left fold*. If `e1` contains an unexpanded pack, the expression is called a *binary right fold*. [*Example 1*:

```cpp
template<typename ...Args>
bool f(Args ...args) {
  return (true && ... && args); // OK
}

template<typename ...Args>
bool f(Args ...args) {
  return (args + ... + args);   // error: both operands contain unexpanded packs
}
```

— *end example*]
