# A.10 Templates [gram.temp]

[Contents](README.md) · up: [Annex A Grammar summary \[gram\]](gram.md) · prev: [A.9 Overloading \[gram.over\]](gram.over.md) · next: [A.11 Exception handling \[gram.except\]](gram.except.md)

```bnf
template-declaration:
  template-head declaration
  template-head concept-definition
```

```bnf
template-head:
  'template' '<' template-parameter-list '>' requires-clauseₒₚₜ
```

```bnf
template-parameter-list:
  template-parameter
  template-parameter-list ',' template-parameter
```

```bnf
requires-clause:
  'requires' constraint-logical-or-expression
```

```bnf
constraint-logical-or-expression:
  constraint-logical-and-expression
  constraint-logical-or-expression '||' constraint-logical-and-expression
```

```bnf
constraint-logical-and-expression:
  primary-expression
  constraint-logical-and-expression '&&' primary-expression
```

```bnf
template-parameter:
  type-parameter
  parameter-declaration
```

```bnf
type-parameter:
  type-parameter-key '...'ₒₚₜ identifierₒₚₜ
  type-parameter-key identifierₒₚₜ '=' type-id
  type-constraint '...'ₒₚₜ identifierₒₚₜ
  type-constraint identifierₒₚₜ '=' type-id
  template-head type-parameter-key '...'ₒₚₜ identifierₒₚₜ
  template-head type-parameter-key identifierₒₚₜ '=' id-expression
```

```bnf
type-parameter-key:
  'class'
  'typename'
```

```bnf
type-constraint:
  nested-name-specifierₒₚₜ concept-name
  nested-name-specifierₒₚₜ concept-name '<' template-argument-listₒₚₜ '>'
```

```bnf
simple-template-id:
  template-name '<' template-argument-listₒₚₜ '>'
```

```bnf
template-id:
  simple-template-id
  operator-function-id '<' template-argument-listₒₚₜ '>'
  literal-operator-id '<' template-argument-listₒₚₜ '>'
```

```bnf
template-name:
  identifier
```

```bnf
template-argument-list:
  template-argument '...'ₒₚₜ
  template-argument-list ',' template-argument '...'ₒₚₜ
```

```bnf
template-argument:
  constant-expression
  type-id
  id-expression
```

```bnf
constraint-expression:
    logical-or-expression
```

```bnf
deduction-guide:
    explicit-specifierₒₚₜ template-name '(' parameter-declaration-clause ')' '->' simple-template-id ';'
```

```bnf
concept-definition:
  'concept' concept-name '=' constraint-expression ';'
```

```bnf
concept-name:
  identifier
```

```bnf
typename-specifier:
  'typename' nested-name-specifier identifier
  'typename' nested-name-specifier 'templateₒₚₜ' simple-template-id
```

```bnf
explicit-instantiation:
  'extern'ₒₚₜ 'template' declaration
```

```bnf
explicit-specialization:
  'template' '<' '>' declaration
```
