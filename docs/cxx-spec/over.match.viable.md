# 12.4.2 Viable functions [over.match.viable]

[Contents](README.md) · up: [12.4 Overload resolution \[over.match\]](over.match.md) · prev: [12.4.1.8 Class template argument deduction \[over.match.class.deduct\]](over.match.class.deduct.md) · next: [12.4.3 Best viable function \[over.match.best\]](over.match.best.md)

**1** From the set of candidate functions constructed for a given context ([[over.match.funcs]](over.match.funcs.md)), a set of viable functions is chosen, from which the best function will be selected by comparing argument conversion sequences and associated constraints ([[temp.constr.decl]](temp.constr.decl.md)) for the best fit ([[over.match.best]](over.match.best.md)). The selection of viable functions considers associated constraints, if any, and relationships between arguments and function parameters other than the ranking of conversion sequences.

**2** First, to be a viable function, a candidate function shall have enough parameters to agree in number with the arguments in the list.

- If there are *m* arguments in the list, all candidate functions having exactly *m* parameters are viable.
- A candidate function having fewer than *m* parameters is viable only if it has an ellipsis in its parameter list ([[dcl.fct]](dcl.fct.md)). For the purposes of overload resolution, any argument for which there is no corresponding parameter is considered to “match the ellipsis” ([[over.ics.ellipsis]](over.ics.ellipsis.md)).
- A candidate function having more than *m* parameters is viable only if all parameters following the *m*<sup></sup>*th* have default arguments ([[dcl.fct.default]](dcl.fct.default.md)). For the purposes of overload resolution, the parameter list is truncated on the right, so that there are exactly *m* parameters.

**3** Second, for a function to be viable, if it has associated constraints ([[temp.constr.decl]](temp.constr.decl.md)), those constraints shall be satisfied ([[temp.constr.constr]](temp.constr.constr.md)).

**4** Third, for `F` to be a viable function, there shall exist for each argument an implicit conversion sequence ([[over.best.ics]](over.best.ics.md)) that converts that argument to the corresponding parameter of `F`. If the parameter has reference type, the implicit conversion sequence includes the operation of binding the reference, and the fact that an lvalue reference to non-`const` cannot be bound to an rvalue and that an rvalue reference cannot be bound to an lvalue can affect the viability of the function (see [[over.ics.ref]](over.ics.ref.md)).
