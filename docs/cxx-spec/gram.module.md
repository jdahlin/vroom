# A.7 Modules [gram.module]

[Contents](README.md) · up: [Annex A Grammar summary \[gram\]](gram.md) · prev: [A.6 Declarations \[gram.dcl\]](gram.dcl.md) · next: [A.8 Classes \[gram.class\]](gram.class.md)

```bnf
module-declaration:
    export-keywordₒₚₜ module-keyword module-name module-partitionₒₚₜ attribute-specifier-seqₒₚₜ ';'
```

```bnf
module-name:
    module-name-qualifierₒₚₜ identifier
```

```bnf
module-partition:
    ':' module-name-qualifierₒₚₜ identifier
```

```bnf
module-name-qualifier:
    identifier '.'
    module-name-qualifier identifier '.'
```

```bnf
export-declaration:
    'export' declaration
    'export' '{' declaration-seqₒₚₜ '}'
    export-keyword module-import-declaration
```

```bnf
module-import-declaration:
    import-keyword module-name attribute-specifier-seqₒₚₜ ';'
    import-keyword module-partition attribute-specifier-seqₒₚₜ ';'
    import-keyword header-name attribute-specifier-seqₒₚₜ ';'
```

```bnf
global-module-fragment:
    module-keyword ';' declaration-seqₒₚₜ
```

```bnf
private-module-fragment:
    module-keyword ':' 'private' ';' declaration-seqₒₚₜ
```
