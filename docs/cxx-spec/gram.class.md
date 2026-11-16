# A.8 Classes [gram.class]

[Contents](README.md) · up: [Annex A Grammar summary \[gram\]](gram.md) · prev: [A.7 Modules \[gram.module\]](gram.module.md) · next: [A.9 Overloading \[gram.over\]](gram.over.md)

```bnf
class-name:
    identifier
    simple-template-id
```

```bnf
class-specifier:
    class-head '{' member-specificationₒₚₜ '}'
```

```bnf
class-head:
    class-key attribute-specifier-seqₒₚₜ class-head-name class-virt-specifierₒₚₜ base-clauseₒₚₜ
    class-key attribute-specifier-seqₒₚₜ base-clauseₒₚₜ
```

```bnf
class-head-name:
    nested-name-specifierₒₚₜ class-name
```

```bnf
class-virt-specifier:
    'final'
```

```bnf
class-key:
    'class'
    'struct'
    'union'
```

```bnf
member-specification:
    member-declaration member-specificationₒₚₜ
    access-specifier ':' member-specificationₒₚₜ
```

```bnf
member-declaration:
    attribute-specifier-seqₒₚₜ decl-specifier-seqₒₚₜ member-declarator-listₒₚₜ ';'
    function-definition
    using-declaration
    using-enum-declaration
    static_assert-declaration
    template-declaration
    explicit-specialization
    deduction-guide
    alias-declaration
    opaque-enum-declaration
    empty-declaration
```

```bnf
member-declarator-list:
    member-declarator
    member-declarator-list ',' member-declarator
```

```bnf
member-declarator:
    declarator virt-specifier-seqₒₚₜ pure-specifierₒₚₜ
    declarator requires-clause
    declarator brace-or-equal-initializerₒₚₜ
    identifierₒₚₜ attribute-specifier-seqₒₚₜ ':' constant-expression brace-or-equal-initializerₒₚₜ
```

```bnf
virt-specifier-seq:
    virt-specifier
    virt-specifier-seq virt-specifier
```

```bnf
virt-specifier:
    'override'
    'final'
```

```bnf
pure-specifier:
    '=' '0'
```

```bnf
conversion-function-id:
    'operator' conversion-type-id
```

```bnf
conversion-type-id:
    type-specifier-seq conversion-declaratorₒₚₜ
```

```bnf
conversion-declarator:
    ptr-operator conversion-declaratorₒₚₜ
```

```bnf
base-clause:
    ':' base-specifier-list
```

```bnf
base-specifier-list:
    base-specifier '...'ₒₚₜ
    base-specifier-list ',' base-specifier '...'ₒₚₜ
```

```bnf
base-specifier:
    attribute-specifier-seqₒₚₜ class-or-decltype
    attribute-specifier-seqₒₚₜ 'virtual' access-specifierₒₚₜ class-or-decltype
    attribute-specifier-seqₒₚₜ access-specifier 'virtual'ₒₚₜ class-or-decltype
```

```bnf
class-or-decltype:
    nested-name-specifierₒₚₜ type-name
    nested-name-specifier 'template' simple-template-id
    decltype-specifier
```

```bnf
access-specifier:
    'private'
    'protected'
    'public'
```

```bnf
ctor-initializer:
    ':' mem-initializer-list
```

```bnf
mem-initializer-list:
    mem-initializer '...'ₒₚₜ
    mem-initializer-list ',' mem-initializer '...'ₒₚₜ
```

```bnf
mem-initializer:
    mem-initializer-id '(' expression-listₒₚₜ ')'
    mem-initializer-id braced-init-list
```

```bnf
mem-initializer-id:
    class-or-decltype
    identifier
```
