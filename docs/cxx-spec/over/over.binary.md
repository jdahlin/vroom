# 12.6.2 Binary operators [over.binary]

[Contents](../README.md) · up: [12.6 Overloaded operators \[over.oper\]](over.oper.md) · prev: [12.6.1 Unary operators \[over.unary\]](over.unary.md) · next: [12.6.2.1 Simple assignment \[over.ass\]](over.ass.md)

**1** A *binary operator function* is a function named `operator@` for a binary operator `@` that is either a non-static member function ([[class.mfct]](../class/class.mfct.md)) with one parameter or a non-member function with two parameters. For an expression `x @ y` with subexpressions *x* and *y*, the operator function is selected by overload resolution ([[over.match.oper]](over.match.oper.md)). If a member function is selected, the expression is interpreted as

```bnf
x '.' 'operator' '@' '(' y ')'
```

Otherwise, if a non-member function is selected, the expression is interpreted as

```bnf
'operator' '@' '(' x ',' y ')'
```

**2** An *equality operator function* is an operator function for an equality operator ([[expr.eq]](../expr/expr.eq.md)). A *relational operator function* is an operator function for a relational operator ([[expr.rel]](../expr/expr.rel.md)). A *three-way comparison operator function* is an operator function for the three-way comparison operator ([[expr.spaceship]](../expr/expr.spaceship.md)). A *comparison operator function* is an equality operator function, a relational operator function, or a three-way comparison operator function.

## Subclauses

- [12.6.2.1 Simple assignment \[over.ass\]](over.ass.md)
