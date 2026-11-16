# 12.5 Address of overloaded function [over.over]

[Contents](../README.md) · up: [12 Overloading \[over\]](over.md) · prev: [12.4.3.2 Ranking implicit conversion sequences \[over.ics.rank\]](over.ics.rank.md) · next: [12.6 Overloaded operators \[over.oper\]](over.oper.md)

**1** A use of a function name without arguments is resolved to a function, a pointer to function, or a pointer to member function for a specific function that is chosen from a set of selected functions determined based on the target type required in the context (if any), as described below. The target can be

- an object or reference being initialized ([[dcl.init]](../dcl/dcl.init.md), [[dcl.init.ref]](../dcl/dcl.init.ref.md), [[dcl.init.list]](../dcl/dcl.init.list.md)),
- the left side of an assignment ([[expr.ass]](../expr/expr.ass.md)),
- a parameter of a function ([[expr.call]](../expr/expr.call.md)),
- a parameter of a user-defined operator ([[over.oper]](over.oper.md)),
- the return value of a function, operator function, or conversion ([[stmt.return]](../stmt/stmt.return.md)),
- an explicit type conversion ([[expr.type.conv]](../expr/expr.type.conv.md), [[expr.static.cast]](../expr/expr.static.cast.md), [[expr.cast]](../expr/expr.cast.md)), or
- a non-type *template-parameter* ([[temp.arg.nontype]](../temp/temp.arg.nontype.md)).

The function name can be preceded by the `&` operator. [*Note 1*: Any redundant set of parentheses surrounding the function name is ignored ([[expr.prim.paren]](../expr/expr.prim.paren.md)). — *end note*]

**2** If there is no target, all non-template functions named are selected. Otherwise, a non-template function with type `F` is selected for the function type `FT` of the target type if `F` (after possibly applying the function pointer conversion ([[conv.fctptr]](../expr/conv.fctptr.md))) is identical to `FT`. [*Note 2*: That is, the class of which the function is a member is ignored when matching a pointer-to-member-function type. — *end note*]

**3** For each function template designated by the name, template argument deduction is done ([[temp.deduct.funcaddr]](../temp/temp.deduct.funcaddr.md)), and if the argument deduction succeeds, the resulting template argument list is used to generate a single function template specialization, which is added to the set of selected functions considered. [*Note 3*: As described in [[temp.arg.explicit]](../temp/temp.arg.explicit.md), if deduction fails and the function template name is followed by an explicit template argument list, the *template-id* is then examined to see whether it identifies a single function template specialization. If it does, the *template-id* is considered to be an lvalue for that function template specialization. The target type is not used in that determination. — *end note*]

**4** Non-member functions and static member functions match targets of function pointer type or reference to function type. Non-static member functions match targets of pointer-to-member-function type. If a non-static member function is selected, the reference to the overloaded function name is required to have the form of a pointer to member as described in [[expr.unary.op]](../expr/expr.unary.op.md).

**5** All functions with associated constraints that are not satisfied ([[temp.constr.decl]](../temp/temp.constr.decl.md)) are eliminated from the set of selected functions. If more than one function in the set remains, all function template specializations in the set are eliminated if the set also contains a function that is not a function template specialization. Any given non-template function `F0` is eliminated if the set contains a second non-template function that is more constrained than `F0` according to the partial ordering rules of [[temp.constr.order]](../temp/temp.constr.order.md). Any given function template specialization `F1` is eliminated if the set contains a second function template specialization whose function template is more specialized than the function template of `F1` according to the partial ordering rules of [[temp.func.order]](../temp/temp.func.order.md). After such eliminations, if any, there shall remain exactly one selected function.

**6** [*Example 1*:

```cpp
int f(double);
int f(int);
int (*pfd)(double) = &f;        // selects f(double)
int (*pfi)(int) = &f;           // selects f(int)
int (*pfe)(...) = &f;           // error: type mismatch
int (&rfi)(int) = f;            // selects f(int)
int (&rfd)(double) = f;         // selects f(double)
void g() {
  (int (*)(int))&f;             // cast expression as selector
}
```

The initialization of `pfe` is ill-formed because no `f()` with type `int(...)` has been declared, and not because of any ambiguity. For another example,

```cpp
struct X {
  int f(int);
  static int f(long);
};

int (X::*p1)(int)  = &X::f;     // OK
int    (*p2)(int)  = &X::f;     // error: mismatch
int    (*p3)(long) = &X::f;     // OK
int (X::*p4)(long) = &X::f;     // error: mismatch
int (X::*p5)(int)  = &(X::f);   // error: wrong syntax for
                                // pointer to member
int    (*p6)(long) = &(X::f);   // OK
```

— *end example*]

**7** [*Note 4*: If `f()` and `g()` are both overloaded functions, the cross product of possibilities must be considered to resolve `f(&g)`, or the equivalent expression `f(g)`. — *end note*]

**8** [*Note 5*: Even if `B` is a public base of `D`, we have

```cpp
D* f();
B* (*p1)() = &f;                // error

void g(D*);
void (*p2)(B*) = &g;            // error
```

— *end note*]
