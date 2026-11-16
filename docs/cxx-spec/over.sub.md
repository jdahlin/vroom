# 12.6.4 Subscripting [over.sub]

[Contents](README.md) · up: [12.6 Overloaded operators \[over.oper\]](over.oper.md) · prev: [12.6.3 Function call \[over.call\]](over.call.md) · next: [12.6.5 Class member access \[over.ref\]](over.ref.md)

**1** A *subscripting operator function* is a function named `operator[]` that is a non-static member function with exactly one parameter. For an expression of the form

```bnf
postfix-expression '[' expr-or-braced-init-list ']'
```

the operator function is selected by overload resolution ([[over.match.oper]](over.match.oper.md)). If a member function is selected, the expression is interpreted as

```bnf
postfix-expression . 'operator' '['']' '(' expr-or-braced-init-list ')'
```

**2** [*Example 1*:

```cpp
struct X {
  Z operator[](std::initializer_list<int>);
};
X x;
x[{1,2,3}] = 7;                 // OK: meaning x.operator[]({1,2,3})
int a[10];
a[{1,2,3}] = 7;                 // error: built-in subscript operator
```

— *end example*]
