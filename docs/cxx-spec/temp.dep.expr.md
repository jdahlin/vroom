# 13.8.2.2 Type-dependent expressions [temp.dep.expr]

[Contents](README.md) · up: [13.8.2 Dependent names \[temp.dep\]](temp.dep.md) · prev: [13.8.2.1 Dependent types \[temp.dep.type\]](temp.dep.type.md) · next: [13.8.2.3 Value-dependent expressions \[temp.dep.constexpr\]](temp.dep.constexpr.md)

**1** Except as described below, an expression is type-dependent if any subexpression is type-dependent.

**2** `this` is type-dependent if the class type of the enclosing member function is dependent ([[temp.dep.type]](temp.dep.type.md)).

**3** An *id-expression* is type-dependent if it is not a concept-id and it contains

- an *identifier* associated by name lookup with one or more declarations declared with a dependent type,
- an *identifier* associated by name lookup with a non-type *template-parameter* declared with a type that contains a placeholder type ([[dcl.spec.auto]](dcl.spec.auto.md)),
- an *identifier* associated by name lookup with a variable declared with a type that contains a placeholder type ([[dcl.spec.auto]](dcl.spec.auto.md)) where the initializer is type-dependent,
- an *identifier* associated by name lookup with one or more declarations of member functions of the current instantiation declared with a return type that contains a placeholder type,
- an *identifier* associated by name lookup with a structured binding declaration ([[dcl.struct.bind]](dcl.struct.bind.md)) whose *brace-or-equal-initializer* is type-dependent,
- the *identifier* `__func__` ([[dcl.fct.def.general]](dcl.fct.def.general.md)), where any enclosing function is a template, a member of a class template, or a generic lambda,
- a *template-id* that is dependent,
- a *conversion-function-id* that specifies a dependent type, or
- a *nested-name-specifier* or a *qualified-id* that names a member of an unknown specialization;

or if it names a dependent member of the current instantiation that is a static data member of type “array of unknown bound of `T`” for some `T` ([[temp.static]](temp.static.md)). Expressions of the following forms are type-dependent only if the type specified by the *type-id*, *simple-type-specifier* or *new-type-id* is dependent, even if any subexpression is type-dependent:

```bnf
simple-type-specifier '(' expression-listₒₚₜ ')'
'::'ₒₚₜ 'new' new-placementₒₚₜ new-type-id new-initializerₒₚₜ
'::'ₒₚₜ 'new' new-placementₒₚₜ '(' type-id ')' new-initializerₒₚₜ
'dynamic_cast' '<' type-id '>' '(' expression ')'
'static_cast' '<' type-id '>' '(' expression ')'
'const_cast' '<' type-id '>' '(' expression ')'
'reinterpret_cast' '<' type-id '>' '(' expression ')'
'(' type-id ')' cast-expression
```

**4** Expressions of the following forms are never type-dependent (because the type of the expression cannot be dependent):

```bnf
literal
'sizeof' unary-expression
'sizeof' '(' type-id ')'
'sizeof' '...' '(' identifier ')'
'alignof' '(' type-id ')'
'typeid' '(' expression ')'
'typeid' '(' type-id ')'
'::'ₒₚₜ 'delete' cast-expression
'::'ₒₚₜ 'delete' '[' ']' cast-expression
'throw' assignment-expressionₒₚₜ
'noexcept' '(' expression ')'
```

[*Note 1*: For the standard library macro `offsetof`, see [[support.types]](lib/support.types.md). — *end note*]

**5** A class member access expression ([[expr.ref]](expr.ref.md)) is type-dependent if the expression refers to a member of the current instantiation and the type of the referenced member is dependent, or the class member access expression refers to a member of an unknown specialization. [*Note 2*: In an expression of the form `x.y` or `xp->y` the type of the expression is usually the type of the member `y` of the class of `x` (or the class pointed to by `xp`). However, if `x` or `xp` refers to a dependent type that is not the current instantiation, the type of `y` is always dependent. If `x` or `xp` refers to a non-dependent type or refers to the current instantiation, the type of `y` is the type of the class member access expression. — *end note*]

**6** A *braced-init-list* is type-dependent if any element is type-dependent or is a pack expansion.

**7** A *fold-expression* is type-dependent.
