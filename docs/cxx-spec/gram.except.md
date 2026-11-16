# A.11 Exception handling [gram.except]

[Contents](README.md) · up: [Annex A Grammar summary \[gram\]](gram.md) · prev: [A.10 Templates \[gram.temp\]](gram.temp.md) · next: [A.12 Preprocessing directives \[gram.cpp\]](gram.cpp.md)

```bnf
try-block:
    'try' compound-statement handler-seq
```

```bnf
function-try-block:
    'try' ctor-initializerₒₚₜ compound-statement handler-seq
```

```bnf
handler-seq:
    handler handler-seqₒₚₜ
```

```bnf
handler:
    'catch' '(' exception-declaration ')' compound-statement
```

```bnf
exception-declaration:
    attribute-specifier-seqₒₚₜ type-specifier-seq declarator
    attribute-specifier-seqₒₚₜ type-specifier-seq abstract-declaratorₒₚₜ
    '...'
```

```bnf
noexcept-specifier:
    'noexcept' '(' constant-expression ')'
    'noexcept'
```
