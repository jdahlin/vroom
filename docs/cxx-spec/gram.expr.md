# A.4 Expressions [gram.expr]

[Contents](README.md) · up: [Annex A Grammar summary \[gram\]](gram.md) · prev: [A.3 Basics \[gram.basic\]](gram.basic.md) · next: [A.5 Statements \[gram.stmt\]](gram.stmt.md)

```bnf
primary-expression:
    literal
    'this'
    '(' expression ')'
    id-expression
    lambda-expression
    fold-expression
    requires-expression
```

```bnf
id-expression:
    unqualified-id
    qualified-id
```

```bnf
unqualified-id:
    identifier
    operator-function-id
    conversion-function-id
    literal-operator-id
    '~' type-name
    '~' decltype-specifier
    template-id
```

```bnf
qualified-id:
    nested-name-specifier 'template'ₒₚₜ unqualified-id
```

```bnf
nested-name-specifier:
    '::'
    type-name '::'
    namespace-name '::'
    decltype-specifier '::'
    nested-name-specifier identifier '::'
    nested-name-specifier 'template'ₒₚₜ simple-template-id '::'
```

```bnf
lambda-expression:
    lambda-introducer lambda-declaratorₒₚₜ compound-statement
    lambda-introducer '<' template-parameter-list '>' requires-clauseₒₚₜ lambda-declaratorₒₚₜ compound-statement
```

```bnf
lambda-introducer:
    '[' lambda-captureₒₚₜ ']'
```

```bnf
lambda-declarator:
    '(' parameter-declaration-clause ')' decl-specifier-seqₒₚₜ
        noexcept-specifierₒₚₜ attribute-specifier-seqₒₚₜ trailing-return-typeₒₚₜ requires-clauseₒₚₜ
```

```bnf
lambda-capture:
    capture-default
    capture-list
    capture-default ',' capture-list
```

```bnf
capture-default:
    '&'
    '='
```

```bnf
capture-list:
    capture
    capture-list ',' capture
```

```bnf
capture:
    simple-capture
    init-capture
```

```bnf
simple-capture:
    identifier '...'ₒₚₜ
    '&' identifier '...'ₒₚₜ
    'this'
    '*' 'this'
```

```bnf
init-capture:
    '...'ₒₚₜ identifier initializer
    '&' '...'ₒₚₜ identifier initializer
```

```bnf
fold-expression:
    '(' cast-expression fold-operator '...' ')'
    '(' '...' fold-operator cast-expression ')'
    '(' cast-expression fold-operator '...' fold-operator cast-expression ')'
```

```bnf
fold-operator: one of
    '+' '-' '*' '/' '%' '^' '&' '|' '<<' '>>'
    '+=' '-=' '*=' '/=' '%=' '^=' '&=' '|=' '<<=' '>>=' '='
    '==' '!=' '<' '>' '<=' '>=' '&&' '||' ',' '.*' '->*'
```

```bnf
requires-expression:
    'requires' requirement-parameter-listₒₚₜ requirement-body
```

```bnf
requirement-parameter-list:
    '(' parameter-declaration-clauseₒₚₜ ')'
```

```bnf
requirement-body:
    '{' requirement-seq '}'
```

```bnf
requirement-seq:
    requirement
    requirement-seq requirement
```

```bnf
requirement:
    simple-requirement
    type-requirement
    compound-requirement
    nested-requirement
```

```bnf
simple-requirement:
    expression ';'
```

```bnf
type-requirement:
    'typename' nested-name-specifierₒₚₜ type-name ';'
```

```bnf
compound-requirement:
    '{' expression '}' 'noexcept'ₒₚₜ return-type-requirementₒₚₜ ';'
```

```bnf
return-type-requirement:
    '->' type-constraint
```

```bnf
nested-requirement:
    'requires' constraint-expression ';'
```

```bnf
postfix-expression:
    primary-expression
    postfix-expression '[' expr-or-braced-init-list ']'
    postfix-expression '(' expression-listₒₚₜ ')'
    simple-type-specifier '(' expression-listₒₚₜ ')'
    typename-specifier '(' expression-listₒₚₜ ')'
    simple-type-specifier braced-init-list
    typename-specifier braced-init-list
    postfix-expression '.' 'template'ₒₚₜ id-expression
    postfix-expression '->' 'template'ₒₚₜ id-expression
    postfix-expression '++'
    postfix-expression '--'
    'dynamic_cast' '<' type-id '>' '(' expression ')'
    'static_cast' '<' type-id '>' '(' expression ')'
    'reinterpret_cast' '<' type-id '>' '(' expression ')'
    'const_cast' '<' type-id '>' '(' expression ')'
    'typeid' '(' expression ')'
    'typeid' '(' type-id ')'
```

```bnf
expression-list:
    initializer-list
```

```bnf
unary-expression:
    postfix-expression
    unary-operator cast-expression
    '++' cast-expression
    '--' cast-expression
    await-expression
    'sizeof' unary-expression
    'sizeof' '(' type-id ')'
    'sizeof' '...' '(' identifier ')'
    'alignof' '(' type-id ')'
    noexcept-expression
    new-expression
    delete-expression
```

```bnf
unary-operator: one of
    '* & + - ! ~'
```

```bnf
await-expression:
    'co_await' cast-expression
```

```bnf
noexcept-expression:
  'noexcept' '(' expression ')'
```

```bnf
new-expression:
    '::'ₒₚₜ 'new' new-placementₒₚₜ new-type-id new-initializerₒₚₜ
    '::'ₒₚₜ 'new' new-placementₒₚₜ '(' type-id ')' new-initializerₒₚₜ
```

```bnf
new-placement:
    '(' expression-list ')'
```

```bnf
new-type-id:
    type-specifier-seq new-declaratorₒₚₜ
```

```bnf
new-declarator:
    ptr-operator new-declaratorₒₚₜ
    noptr-new-declarator
```

```bnf
noptr-new-declarator:
    '[' expressionₒₚₜ ']' attribute-specifier-seqₒₚₜ
    noptr-new-declarator '[' constant-expression ']' attribute-specifier-seqₒₚₜ
```

```bnf
new-initializer:
    '(' expression-listₒₚₜ ')'
    braced-init-list
```

```bnf
delete-expression:
    '::'ₒₚₜ 'delete' cast-expression
    '::'ₒₚₜ 'delete' '[' ']' cast-expression
```

```bnf
cast-expression:
    unary-expression
    '(' type-id ')' cast-expression
```

```bnf
pm-expression:
    cast-expression
    pm-expression '.*' cast-expression
    pm-expression '->*' cast-expression
```

```bnf
multiplicative-expression:
    pm-expression
    multiplicative-expression '*' pm-expression
    multiplicative-expression '/' pm-expression
    multiplicative-expression '%' pm-expression
```

```bnf
additive-expression:
    multiplicative-expression
    additive-expression '+' multiplicative-expression
    additive-expression '-' multiplicative-expression
```

```bnf
shift-expression:
    additive-expression
    shift-expression '<<' additive-expression
    shift-expression '>>' additive-expression
```

```bnf
compare-expression:
    shift-expression
    compare-expression '<=>' shift-expression
```

```bnf
relational-expression:
    compare-expression
    relational-expression '<' compare-expression
    relational-expression '>' compare-expression
    relational-expression '<=' compare-expression
    relational-expression '>=' compare-expression
```

```bnf
equality-expression:
    relational-expression
    equality-expression '==' relational-expression
    equality-expression '!=' relational-expression
```

```bnf
and-expression:
    equality-expression
    and-expression '&' equality-expression
```

```bnf
exclusive-or-expression:
    and-expression
    exclusive-or-expression '^' and-expression
```

```bnf
inclusive-or-expression:
    exclusive-or-expression
    inclusive-or-expression '|' exclusive-or-expression
```

```bnf
logical-and-expression:
    inclusive-or-expression
    logical-and-expression '&&' inclusive-or-expression
```

```bnf
logical-or-expression:
    logical-and-expression
    logical-or-expression '||' logical-and-expression
```

```bnf
conditional-expression:
    logical-or-expression
    logical-or-expression '?' expression ':' assignment-expression
```

```bnf
  yield-expression:
  'co_yield' assignment-expression
  'co_yield' braced-init-list
```

```bnf
throw-expression:
    'throw'  assignment-expressionₒₚₜ
```

```bnf
assignment-expression:
    conditional-expression
    yield-expression
    throw-expression
    logical-or-expression assignment-operator initializer-clause
```

```bnf
assignment-operator: one of
    '= *= /= %= += -= >>= <<= &= ^= |='
```

```bnf
expression:
    assignment-expression
    expression ',' assignment-expression
```

```bnf
constant-expression:
    conditional-expression
```
