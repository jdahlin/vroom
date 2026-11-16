# 7.5.7.4 Nested requirements [expr.prim.req.nested]

[Contents](../README.md) · up: [7.5.7 Requires expressions \[expr.prim.req\]](expr.prim.req.md) · prev: [7.5.7.3 Compound requirements \[expr.prim.req.compound\]](expr.prim.req.compound.md) · next: [7.6 Compound expressions \[expr.compound\]](expr.compound.md)

```bnf
nested-requirement:
    'requires' constraint-expression ';'
```

**1** A *nested-requirement* can be used to specify additional constraints in terms of local parameters. The *constraint-expression* shall be satisfied ([[temp.constr.decl]](../temp/temp.constr.decl.md)) by the substituted template arguments, if any. Substitution of template arguments into a *nested-requirement* does not result in substitution into the *constraint-expression* other than as specified in [[temp.constr.constr]](../temp/temp.constr.constr.md). [*Example 1*:

```cpp
template<typename U> concept C = sizeof(U) == 1;

template<typename T> concept D = requires (T t) {
  requires C<decltype (+t)>;
};
```

`D<T>` is satisfied if `sizeof(decltype (+t)) == 1` ([[temp.constr.atomic]](../temp/temp.constr.atomic.md)). — *end example*]

**2** A local parameter shall only appear as an unevaluated operand ([[expr.prop]](expr.prop.md)) within the *constraint-expression*. [*Example 2*:

```cpp
template<typename T> concept C = requires (T a) {
  requires sizeof(a) == 4;      // OK
  requires a == 0;              // error: evaluation of a constraint variable
};
```

— *end example*]
