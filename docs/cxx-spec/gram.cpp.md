# A.12 Preprocessing directives [gram.cpp]

[Contents](README.md) · up: [Annex A Grammar summary \[gram\]](gram.md) · prev: [A.11 Exception handling \[gram.except\]](gram.except.md) · next: [Annex B Implementation quantities \[implimits\]](implimits.md)

```bnf
preprocessing-file:
    groupₒₚₜ
    module-file
```

```bnf
module-file:
    pp-global-module-fragmentₒₚₜ pp-module groupₒₚₜ pp-private-module-fragmentₒₚₜ
```

```bnf
pp-global-module-fragment:
    'module' ';' new-line groupₒₚₜ
```

```bnf
pp-private-module-fragment:
    'module' ':' 'private' ';' new-line groupₒₚₜ
```

```bnf
group:
    group-part
    group group-part
```

```bnf
group-part:
    control-line
    if-section
    text-line
    '#' conditionally-supported-directive
```

```bnf
control-line:
    '# include' pp-tokens new-line
    pp-import
    '# define' identifier replacement-list new-line
    '# define' identifier lparen identifier-listₒₚₜ ')' replacement-list new-line
    '# define' identifier lparen '... )' replacement-list new-line
    '# define' identifier lparen identifier-list ', ... )' replacement-list new-line
    '# undef' identifier new-line
    '# line' pp-tokens new-line
    '# error' pp-tokensₒₚₜ new-line
    '# pragma' pp-tokensₒₚₜ new-line
    '#'new-line
```

```bnf
if-section:
    if-group elif-groupsₒₚₜ else-groupₒₚₜ endif-line
```

```bnf
if-group:
    '# if' constant-expression new-line groupₒₚₜ
    '# ifdef' identifier new-line groupₒₚₜ
    '# ifndef' identifier new-line groupₒₚₜ
```

```bnf
elif-groups:
    elif-group
    elif-groups elif-group
```

```bnf
elif-group:
    '# elif' constant-expression new-line groupₒₚₜ
```

```bnf
else-group:
    '# else' new-line groupₒₚₜ
```

```bnf
endif-line:
    '# endif' new-line
```

```bnf
text-line:
    pp-tokensₒₚₜ new-line
```

```bnf
conditionally-supported-directive:
    pp-tokens new-line
```

```bnf
lparen:
    a '(' character not immediately preceded by white-space
```

```bnf
identifier-list:
    identifier
    identifier-list ',' identifier
```

```bnf
replacement-list:
    pp-tokensₒₚₜ
```

```bnf
pp-tokens:
    preprocessing-token
    pp-tokens preprocessing-token
```

```bnf
new-line:
    the new-line character
```

```bnf
defined-macro-expression:
    'defined' identifier
    'defined (' identifier ')'
```

```bnf
h-preprocessing-token:
    any preprocessing-token other than '>'
```

```bnf
h-pp-tokens:
    h-preprocessing-token
    h-pp-tokens h-preprocessing-token
```

```bnf
header-name-tokens:
    string-literal
    '<' h-pp-tokens '>'
```

```bnf
has-include-expression:
    '__has_include' '(' header-name ')'
    '__has_include' '(' header-name-tokens ')'
```

```bnf
has-attribute-expression:
    '__has_cpp_attribute (' pp-tokens ')'
```

```bnf
pp-module:
    'export'ₒₚₜ 'module' pp-tokensₒₚₜ ';' new-line
```

```bnf
pp-import:
    'export'ₒₚₜ 'import' header-name pp-tokensₒₚₜ ';' new-line
    'export'ₒₚₜ 'import' header-name-tokens pp-tokensₒₚₜ ';' new-line
    'export'ₒₚₜ 'import' pp-tokens ';' new-line
```

```bnf
va-opt-replacement:
    '__VA_OPT__ (' pp-tokensₒₚₜ ')'
```
