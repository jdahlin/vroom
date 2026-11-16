# 12.4.1.1 Function call syntax [over.match.call]

[Contents](README.md) · up: [12.4.1 Candidate functions and argument lists \[over.match.funcs\]](over.match.funcs.md) · prev: [12.4.1 Candidate functions and argument lists \[over.match.funcs\]](over.match.funcs.md) · next: [12.4.1.1.1 Call to named function \[over.call.func\]](over.call.func.md)

**1** In a function call ([[expr.call]](expr.call.md))

```bnf
postfix-expression '(' expression-listₒₚₜ ')'
```

if the *postfix-expression* names at least one function or function template, overload resolution is applied as specified in [[over.call.func]](over.call.func.md). If the *postfix-expression* denotes an object of class type, overload resolution is applied as specified in [[over.call.object]](over.call.object.md).

**2** If the *postfix-expression* is the address of an overload set, overload resolution is applied using that set as described above. If the function selected by overload resolution is a non-static member function, the program is ill-formed. [*Note 1*: The resolution of the address of an overload set in other contexts is described in [[over.over]](over.over.md). — *end note*]

## Subclauses

- [12.4.1.1.1 Call to named function \[over.call.func\]](over.call.func.md)
- [12.4.1.1.2 Call to object of class type \[over.call.object\]](over.call.object.md)
