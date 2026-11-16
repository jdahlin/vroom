# 20.15.4 Unary type traits [meta.unary]

[Contents](../../README.md) · up: [20.15 Metaprogramming and type traits \[meta\]](meta.md) · prev: [20.15.3 Helper classes \[meta.help\]](meta.help.md) · next: [20.15.4.1 Primary type categories \[meta.unary.cat\]](meta.unary.cat.md)

**1** This subclause contains templates that may be used to query the properties of a type at compile time.

**2** Each of these templates shall be a *Cpp17UnaryTypeTrait* ([[meta.rqmts]](meta.rqmts.md)) with a base characteristic of `true_type` if the corresponding condition is `true`, otherwise `false_type`.

## Subclauses

- [20.15.4.1 Primary type categories \[meta.unary.cat\]](meta.unary.cat.md)
- [20.15.4.2 Composite type traits \[meta.unary.comp\]](meta.unary.comp.md)
- [20.15.4.3 Type properties \[meta.unary.prop\]](meta.unary.prop.md)
