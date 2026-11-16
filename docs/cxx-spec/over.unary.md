# 12.6.1 Unary operators [over.unary]

[Contents](README.md) · up: [12.6 Overloaded operators \[over.oper\]](over.oper.md) · prev: [12.6 Overloaded operators \[over.oper\]](over.oper.md) · next: [12.6.2 Binary operators \[over.binary\]](over.binary.md)

**1** A *prefix unary operator function* is a function named `operator@` for a prefix *unary-operator* `@` ([[expr.unary.op]](expr.unary.op.md)) that is either a non-static member function ([[class.mfct]](class.mfct.md)) with no parameters or a non-member function with one parameter. For a *unary-expression* of the form `@ cast-expression`, the operator function is selected by overload resolution ([[over.match.oper]](over.match.oper.md)). If a member function is selected, the expression is interpreted as

```bnf
cast-expression '.' 'operator' '@' '('')'
```

Otherwise, if a non-member function is selected, the expression is interpreted as

```bnf
'operator' '@' '(' cast-expression ')'
```

[*Note 1*: The operators `++` and `--` ([[expr.pre.incr]](expr.pre.incr.md)) are described in [[over.inc]](over.inc.md). — *end note*]

**2** The unary and binary forms of the same operator are considered to have the same name. [*Note 2*: Consequently, a unary operator can hide a binary operator from an enclosing scope, and vice versa. — *end note*]
