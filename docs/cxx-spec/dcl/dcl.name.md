# 9.3.1 Type names [dcl.name]

[Contents](../README.md) · up: [9.3 Declarators \[dcl.decl\]](dcl.decl.md) · prev: [9.3 Declarators \[dcl.decl\]](dcl.decl.md) · next: [9.3.2 Ambiguity resolution \[dcl.ambig.res\]](dcl.ambig.res.md)

**1** To specify type conversions explicitly, and as an argument of `sizeof`, `alignof`, `new`, or `typeid`, the name of a type shall be specified. This can be done with a *type-id*, which is syntactically a declaration for a variable or function of that type that omits the name of the entity.

```bnf
type-id:
    type-specifier-seq abstract-declaratorₒₚₜ
```

```bnf
defining-type-id:
    defining-type-specifier-seq abstract-declaratorₒₚₜ
```

```bnf
abstract-declarator:
    ptr-abstract-declarator
    noptr-abstract-declaratorₒₚₜ parameters-and-qualifiers trailing-return-type
    abstract-pack-declarator
```

```bnf
ptr-abstract-declarator:
    noptr-abstract-declarator
    ptr-operator ptr-abstract-declaratorₒₚₜ
```

```bnf
noptr-abstract-declarator:
    noptr-abstract-declaratorₒₚₜ parameters-and-qualifiers
    noptr-abstract-declaratorₒₚₜ '[' constant-expressionₒₚₜ ']' attribute-specifier-seqₒₚₜ
    '(' ptr-abstract-declarator ')'
```

```bnf
abstract-pack-declarator:
    noptr-abstract-pack-declarator
    ptr-operator abstract-pack-declarator
```

```bnf
noptr-abstract-pack-declarator:
    noptr-abstract-pack-declarator parameters-and-qualifiers
    noptr-abstract-pack-declarator '[' constant-expressionₒₚₜ ']' attribute-specifier-seqₒₚₜ
    '...'
```

It is possible to identify uniquely the location in the *abstract-declarator* where the identifier would appear if the construction were a declarator in a declaration. The named type is then the same as the type of the hypothetical identifier. [*Example 1*:

```cpp
int                 // int i
int *               // int *pi
int *[3]            // int *p[3]
int (*)[3]          // int (*p3i)[3]
int *()             // int *f()
int (*)(double)     // int (*pf)(double)
```

name respectively the types “`int`”, “pointer to `int`”, “array of 3 pointers to `int`”, “pointer to array of 3 `int`”, “function of (no parameters) returning pointer to `int`”, and “pointer to a function of (`double`) returning `int`”. — *end example*]

**2** A type can also be named (often more easily) by using a `typedef` ([[dcl.typedef]](dcl.typedef.md)).
