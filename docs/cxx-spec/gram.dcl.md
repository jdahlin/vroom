# A.6 Declarations [gram.dcl]

[Contents](README.md) · up: [Annex A Grammar summary \[gram\]](gram.md) · prev: [A.5 Statements \[gram.stmt\]](gram.stmt.md) · next: [A.7 Modules \[gram.module\]](gram.module.md)

```bnf
declaration-seq:
    declaration
    declaration-seq declaration
```

```bnf
declaration:
    block-declaration
    nodeclspec-function-declaration
    function-definition
    template-declaration
    deduction-guide
    explicit-instantiation
    explicit-specialization
    export-declaration
    linkage-specification
    namespace-definition
    empty-declaration
    attribute-declaration
    module-import-declaration
```

```bnf
block-declaration:
    simple-declaration
    asm-declaration
    namespace-alias-definition
    using-declaration
    using-enum-declaration
    using-directive
    static_assert-declaration
    alias-declaration
    opaque-enum-declaration
```

```bnf
nodeclspec-function-declaration:
    attribute-specifier-seqₒₚₜ declarator ';'
```

```bnf
alias-declaration:
    'using' identifier attribute-specifier-seqₒₚₜ '=' defining-type-id ';'
```

```bnf
simple-declaration:
    decl-specifier-seq init-declarator-listₒₚₜ ';'
    attribute-specifier-seq decl-specifier-seq init-declarator-list ';'
    attribute-specifier-seqₒₚₜ decl-specifier-seq ref-qualifierₒₚₜ '[' identifier-list ']' initializer ';'
```

```bnf
static_assert-declaration:
  'static_assert' '(' constant-expression ')' ';'
  'static_assert' '(' constant-expression ',' string-literal ')' ';'
```

```bnf
empty-declaration:
    ';'
```

```bnf
attribute-declaration:
    attribute-specifier-seq ';'
```

```bnf
decl-specifier:
    storage-class-specifier
    defining-type-specifier
    function-specifier
    'friend'
    'typedef'
    'constexpr'
    'consteval'
    'constinit'
    'inline'
```

```bnf
decl-specifier-seq:
    decl-specifier attribute-specifier-seqₒₚₜ
    decl-specifier decl-specifier-seq
```

```bnf
storage-class-specifier:
    'static'
    'thread_local'
    'extern'
    'mutable'
```

```bnf
function-specifier:
    'virtual'
    explicit-specifier
```

```bnf
explicit-specifier:
    'explicit' '(' constant-expression ')'
    'explicit'
```

```bnf
typedef-name:
    identifier
    simple-template-id
```

```bnf
type-specifier:
  simple-type-specifier
  elaborated-type-specifier
  typename-specifier
  cv-qualifier
```

```bnf
type-specifier-seq:
    type-specifier attribute-specifier-seqₒₚₜ
    type-specifier type-specifier-seq
```

```bnf
defining-type-specifier:
    type-specifier
    class-specifier
    enum-specifier
```

```bnf
defining-type-specifier-seq:
  defining-type-specifier attribute-specifier-seqₒₚₜ
  defining-type-specifier defining-type-specifier-seq
```

```bnf
simple-type-specifier:
    nested-name-specifierₒₚₜ type-name
    nested-name-specifier 'template' simple-template-id
    decltype-specifier
    placeholder-type-specifier
    nested-name-specifierₒₚₜ template-name
    'char'
    'char8_t'
    'char16_t'
    'char32_t'
    'wchar_t'
    'bool'
    'short'
    'int'
    'long'
    'signed'
    'unsigned'
    'float'
    'double'
    'void'
```

```bnf
type-name:
    class-name
    enum-name
    typedef-name
```

```bnf
elaborated-type-specifier:
    class-key attribute-specifier-seqₒₚₜ nested-name-specifierₒₚₜ identifier
    class-key simple-template-id
    class-key nested-name-specifier 'template'ₒₚₜ simple-template-id
    elaborated-enum-specifier
```

```bnf
elaborated-enum-specifier:
    'enum' nested-name-specifierₒₚₜ identifier
```

```bnf
decltype-specifier:
  'decltype' '(' expression ')'
```

```bnf
placeholder-type-specifier:
  type-constraintₒₚₜ 'auto'
  type-constraintₒₚₜ 'decltype' '(' 'auto' ')'
```

```bnf
init-declarator-list:
    init-declarator
    init-declarator-list ',' init-declarator
```

```bnf
init-declarator:
    declarator initializerₒₚₜ
    declarator requires-clause
```

```bnf
declarator:
    ptr-declarator
    noptr-declarator parameters-and-qualifiers trailing-return-type
```

```bnf
ptr-declarator:
    noptr-declarator
    ptr-operator ptr-declarator
```

```bnf
noptr-declarator:
    declarator-id attribute-specifier-seqₒₚₜ
    noptr-declarator parameters-and-qualifiers
    noptr-declarator '[' constant-expressionₒₚₜ ']' attribute-specifier-seqₒₚₜ
    '(' ptr-declarator ')'
```

```bnf
parameters-and-qualifiers:
    '(' parameter-declaration-clause ')' cv-qualifier-seqₒₚₜ
        ref-qualifierₒₚₜ noexcept-specifierₒₚₜ attribute-specifier-seqₒₚₜ
```

```bnf
trailing-return-type:
    '->' type-id
```

```bnf
ptr-operator:
    '*' attribute-specifier-seqₒₚₜ cv-qualifier-seqₒₚₜ
    '&' attribute-specifier-seqₒₚₜ
    '&&' attribute-specifier-seqₒₚₜ
    nested-name-specifier '*' attribute-specifier-seqₒₚₜ cv-qualifier-seqₒₚₜ
```

```bnf
cv-qualifier-seq:
    cv-qualifier cv-qualifier-seqₒₚₜ
```

```bnf
cv-qualifier:
    'const'
    'volatile'
```

```bnf
ref-qualifier:
    '&'
    '&&'
```

```bnf
declarator-id:
    '...'ₒₚₜ id-expression
```

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

```bnf
parameter-declaration-clause:
    parameter-declaration-listₒₚₜ '...'ₒₚₜ
    parameter-declaration-list ',' '...'
```

```bnf
parameter-declaration-list:
    parameter-declaration
    parameter-declaration-list ',' parameter-declaration
```

```bnf
parameter-declaration:
    attribute-specifier-seqₒₚₜ decl-specifier-seq declarator
    attribute-specifier-seqₒₚₜ decl-specifier-seq declarator '=' initializer-clause
    attribute-specifier-seqₒₚₜ decl-specifier-seq abstract-declaratorₒₚₜ
    attribute-specifier-seqₒₚₜ decl-specifier-seq abstract-declaratorₒₚₜ '=' initializer-clause
```

```bnf
initializer:
    brace-or-equal-initializer
    '(' expression-list ')'
```

```bnf
brace-or-equal-initializer:
    '=' initializer-clause
    braced-init-list
```

```bnf
initializer-clause:
    assignment-expression
    braced-init-list
```

```bnf
braced-init-list:
    '{' initializer-list ','ₒₚₜ '}'
    '{' designated-initializer-list ','ₒₚₜ '}'
    '{' '}'
```

```bnf
initializer-list:
    initializer-clause '...'ₒₚₜ
    initializer-list ',' initializer-clause '...'ₒₚₜ
```

```bnf
designated-initializer-list:
    designated-initializer-clause
    designated-initializer-list ',' designated-initializer-clause
```

```bnf
designated-initializer-clause:
    designator brace-or-equal-initializer
```

```bnf
designator:
    '.' identifier
```

```bnf
expr-or-braced-init-list:
    expression
    braced-init-list
```

```bnf
function-definition:
    attribute-specifier-seqₒₚₜ decl-specifier-seqₒₚₜ declarator virt-specifier-seqₒₚₜ function-body
    attribute-specifier-seqₒₚₜ decl-specifier-seqₒₚₜ declarator requires-clause function-body
```

```bnf
function-body:
    ctor-initializerₒₚₜ compound-statement
    function-try-block
    '=' 'default' ';'
    '=' 'delete' ';'
```

```bnf
enum-name:
    identifier
```

```bnf
enum-specifier:
    enum-head '{' enumerator-listₒₚₜ '}'
    enum-head '{' enumerator-list ',' '}'
```

```bnf
enum-head:
    enum-key attribute-specifier-seqₒₚₜ enum-head-nameₒₚₜ enum-baseₒₚₜ
```

```bnf
enum-head-name:
    nested-name-specifierₒₚₜ identifier
```

```bnf
opaque-enum-declaration:
    enum-key attribute-specifier-seqₒₚₜ enum-head-name enum-baseₒₚₜ ';'
```

```bnf
enum-key:
    'enum'
    'enum' 'class'
    'enum' 'struct'
```

```bnf
enum-base:
    ':' type-specifier-seq
```

```bnf
enumerator-list:
    enumerator-definition
    enumerator-list ',' enumerator-definition
```

```bnf
enumerator-definition:
    enumerator
    enumerator '=' constant-expression
```

```bnf
enumerator:
    identifier attribute-specifier-seqₒₚₜ
```

```bnf
using-enum-declaration:
    'using' elaborated-enum-specifier ';'
```

```bnf
namespace-name:
        identifier
        namespace-alias
```

```bnf
namespace-definition:
        named-namespace-definition
        unnamed-namespace-definition
        nested-namespace-definition
```

```bnf
named-namespace-definition:
        'inline'ₒₚₜ 'namespace' attribute-specifier-seqₒₚₜ identifier '{' namespace-body '}'
```

```bnf
unnamed-namespace-definition:
        'inline'ₒₚₜ 'namespace' attribute-specifier-seqₒₚₜ '{' namespace-body '}'
```

```bnf
nested-namespace-definition:
        'namespace' enclosing-namespace-specifier '::' 'inline'ₒₚₜ identifier '{' namespace-body '}'
```

```bnf
enclosing-namespace-specifier:
        identifier
        enclosing-namespace-specifier '::' 'inline'ₒₚₜ identifier
```

```bnf
namespace-body:
        declaration-seqₒₚₜ
```

```bnf
namespace-alias:
        identifier
```

```bnf
namespace-alias-definition:
        'namespace' identifier '=' qualified-namespace-specifier ';'
```

```bnf
qualified-namespace-specifier:
    nested-name-specifierₒₚₜ namespace-name
```

```bnf
using-directive:
    attribute-specifier-seqₒₚₜ 'using' 'namespace' nested-name-specifierₒₚₜ namespace-name ';'
```

```bnf
using-declaration:
    'using' using-declarator-list ';'
```

```bnf
using-declarator-list:
    using-declarator '...'ₒₚₜ
    using-declarator-list ',' using-declarator '...'ₒₚₜ
```

```bnf
using-declarator:
    'typename'ₒₚₜ nested-name-specifier unqualified-id
```

```bnf
asm-declaration:
    attribute-specifier-seqₒₚₜ 'asm' '(' string-literal ')' ';'
```

```bnf
linkage-specification:
    'extern' string-literal '{' declaration-seqₒₚₜ '}'
    'extern' string-literal declaration
```

```bnf
attribute-specifier-seq:
  attribute-specifier-seqₒₚₜ attribute-specifier
```

```bnf
attribute-specifier:
  '[' '[' attribute-using-prefixₒₚₜ attribute-list ']' ']'
  alignment-specifier
```

```bnf
alignment-specifier:
  'alignas' '(' type-id '...'ₒₚₜ ')'
  'alignas' '(' constant-expression '...'ₒₚₜ ')'
```

```bnf
attribute-using-prefix:
  'using' attribute-namespace ':'
```

```bnf
attribute-list:
  attributeₒₚₜ
  attribute-list ',' attributeₒₚₜ
  attribute '...'
  attribute-list ',' attribute '...'
```

```bnf
attribute:
    attribute-token attribute-argument-clauseₒₚₜ
```

```bnf
attribute-token:
    identifier
    attribute-scoped-token
```

```bnf
attribute-scoped-token:
    attribute-namespace '::' identifier
```

```bnf
attribute-namespace:
    identifier
```

```bnf
attribute-argument-clause:
    '(' balanced-token-seqₒₚₜ ')'
```

```bnf
balanced-token-seq:
    balanced-token
    balanced-token-seq balanced-token
```

```bnf
balanced-token:
    '(' balanced-token-seqₒₚₜ ')'
    '[' balanced-token-seqₒₚₜ ']'
    '{' balanced-token-seqₒₚₜ '}'
    any token other than a parenthesis, a bracket, or a brace
```
