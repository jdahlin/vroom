# 12.6.3 Function call [over.call]

[Contents](../README.md) · up: [12.6 Overloaded operators \[over.oper\]](over.oper.md) · prev: [12.6.2.1 Simple assignment \[over.ass\]](over.ass.md) · next: [12.6.4 Subscripting \[over.sub\]](over.sub.md)

**1** A *function call operator function* is a function named `operator()` that is a non-static member function with an arbitrary number of parameters. It may have default arguments. For an expression of the form

```bnf
postfix-expression '(' expression-listₒₚₜ ')'
```

where the *postfix-expression* is of class type, the operator function is selected by overload resolution ([[over.call.object]](over.call.object.md)). If a surrogate call function for a conversion function named `operator` *conversion-type-id* is selected, the expression is interpreted as

```bnf
postfix-expression '.' 'operator' conversion-type-id '('')' '(' expression-listₒₚₜ ')'
```

Otherwise, the expression is interpreted as

```bnf
postfix-expression '.' 'operator' '('')' '(' expression-listₒₚₜ ')'
```
