# 12.6.5 Class member access [over.ref]

[Contents](README.md) · up: [12.6 Overloaded operators \[over.oper\]](over.oper.md) · prev: [12.6.4 Subscripting \[over.sub\]](over.sub.md) · next: [12.6.6 Increment and decrement \[over.inc\]](over.inc.md)

**1** A *class member access operator function* is a function named `operator->` that is a non-static member function taking no parameters. For an expression of the form

```bnf
postfix-expression '->' 'template'ₒₚₜ id-expression
```

the operator function is selected by overload resolution ([[over.match.oper]](over.match.oper.md)), and the expression is interpreted as

```bnf
'(' postfix-expression . 'operator' '->' '('')' ')' '->' 'template'ₒₚₜ id-expression
```
