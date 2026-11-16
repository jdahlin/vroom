# 13.8.2.3 Value-dependent expressions [temp.dep.constexpr]

[Contents](../README.md) · up: [13.8.2 Dependent names \[temp.dep\]](temp.dep.md) · prev: [13.8.2.2 Type-dependent expressions \[temp.dep.expr\]](temp.dep.expr.md) · next: [13.8.2.4 Dependent template arguments \[temp.dep.temp\]](temp.dep.temp.md)

**1** Except as described below, an expression used in a context where a constant expression is required is value-dependent if any subexpression is value-dependent.

**2** An *id-expression* is value-dependent if:

- it is a concept-id and any of its arguments are dependent,
- it is type-dependent,
- it is the name of a non-type template parameter,
- it names a static data member that is a dependent member of the current instantiation and is not initialized in a *member-declarator*,
- it names a static member function that is a dependent member of the current instantiation, or
- it names a potentially-constant variable ([[expr.const]](../expr/expr.const.md)) that is initialized with an expression that is value-dependent.

Expressions of the following form are value-dependent if the *unary-expression* or *expression* is type-dependent or the *type-id* is dependent:

```bnf
'sizeof' unary-expression
'sizeof' '(' type-id ')'
'typeid' '(' expression ')'
'typeid' '(' type-id ')'
'alignof' '(' type-id ')'
'noexcept' '(' expression ')'
```

[*Note 1*: For the standard library macro `offsetof`, see [[support.types]](../lib/support/support.types.md). — *end note*]

**3** Expressions of the following form are value-dependent if either the *type-id* or *simple-type-specifier* is dependent or the *expression* or *cast-expression* is value-dependent:

```bnf
simple-type-specifier '(' expression-listₒₚₜ ')'
'static_cast' '<' type-id '>' '(' expression ')'
'const_cast' '<' type-id '>' '(' expression ')'
'reinterpret_cast' '<' type-id '>' '(' expression ')'
'(' type-id ')' cast-expression
```

**4** Expressions of the following form are value-dependent:

```bnf
'sizeof' '...' '(' identifier ')'
fold-expression
```

**5** An expression of the form `&`*qualified-id* where the *qualified-id* names a dependent member of the current instantiation is value-dependent. An expression of the form `&`*cast-expression* is also value-dependent if evaluating *cast-expression* as a core constant expression ([[expr.const]](../expr/expr.const.md)) succeeds and the result of the evaluation refers to a templated entity that is an object with static or thread storage duration or a member function.
