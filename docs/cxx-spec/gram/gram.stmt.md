# A.5 Statements [gram.stmt]

[Contents](../README.md) · up: [Annex A Grammar summary \[gram\]](gram.md) · prev: [A.4 Expressions \[gram.expr\]](gram.expr.md) · next: [A.6 Declarations \[gram.dcl\]](gram.dcl.md)

```bnf
statement:
    labeled-statement
    attribute-specifier-seqₒₚₜ expression-statement
    attribute-specifier-seqₒₚₜ compound-statement
    attribute-specifier-seqₒₚₜ selection-statement
    attribute-specifier-seqₒₚₜ iteration-statement
    attribute-specifier-seqₒₚₜ jump-statement
    declaration-statement
    attribute-specifier-seqₒₚₜ try-block

init-statement:
    expression-statement
    simple-declaration

condition:
    expression
    attribute-specifier-seqₒₚₜ decl-specifier-seq declarator brace-or-equal-initializer
```

```bnf
labeled-statement:
    attribute-specifier-seqₒₚₜ identifier ':' statement
    attribute-specifier-seqₒₚₜ 'case' constant-expression ':' statement
    attribute-specifier-seqₒₚₜ 'default' ':' statement
```

```bnf
expression-statement:
    expressionₒₚₜ ';'
```

```bnf
compound-statement:
    '{' statement-seqₒₚₜ '}'
```

```bnf
statement-seq:
    statement
    statement-seq statement
```

```bnf
selection-statement:
    'if' 'constexpr'ₒₚₜ '(' init-statementₒₚₜ condition ')' statement
    'if' 'constexpr'ₒₚₜ '(' init-statementₒₚₜ condition ')' statement 'else' statement
    'switch' '(' init-statementₒₚₜ condition ')' statement
```

```bnf
iteration-statement:
    'while' '(' condition ')' statement
    'do' statement 'while' '(' expression ')' ';'
    'for' '(' init-statement conditionₒₚₜ ';' expressionₒₚₜ ')' statement
    'for' '(' init-statementₒₚₜ for-range-declaration ':' for-range-initializer ')' statement
```

```bnf
for-range-declaration:
    attribute-specifier-seqₒₚₜ decl-specifier-seq declarator
    attribute-specifier-seqₒₚₜ decl-specifier-seq ref-qualifierₒₚₜ '[' identifier-list ']'
```

```bnf
for-range-initializer:
    expr-or-braced-init-list
```

```bnf
jump-statement:
    'break' ';'
    'continue' ';'
    'return' expr-or-braced-init-listₒₚₜ ';'
    coroutine-return-statement
    'goto' identifier ';'
```

```bnf
coroutine-return-statement:
    'co_return' expr-or-braced-init-listₒₚₜ ';'
```

```bnf
declaration-statement:
    block-declaration
```
