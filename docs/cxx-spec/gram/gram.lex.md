# A.2 Lexical conventions [gram.lex]

[Contents](../README.md) · up: [Annex A Grammar summary \[gram\]](gram.md) · prev: [A.1 Keywords \[gram.key\]](gram.key.md) · next: [A.3 Basics \[gram.basic\]](gram.basic.md)

```bnf
hex-quad:
    hexadecimal-digit hexadecimal-digit hexadecimal-digit hexadecimal-digit
```

```bnf
universal-character-name:
    '\u' hex-quad
    '\U' hex-quad hex-quad
```

```bnf
preprocessing-token:
    header-name
    import-keyword
    module-keyword
    export-keyword
    identifier
    pp-number
    character-literal
    user-defined-character-literal
    string-literal
    user-defined-string-literal
    preprocessing-op-or-punc
    each non-white-space character that cannot be one of the above
```

```bnf
token:
    identifier
    keyword
    literal
    operator-or-punctuator
```

```bnf
header-name:
    '<' h-char-sequence '>'
    '"' q-char-sequence '"'
```

```bnf
h-char-sequence:
    h-char
    h-char-sequence h-char
```

```bnf
h-char:
    any member of the source character set except new-line and '>'
```

```bnf
q-char-sequence:
    q-char
    q-char-sequence q-char
```

```bnf
q-char:
    any member of the source character set except new-line and '"'
```

```bnf
pp-number:
    digit
    '.' digit
    pp-number digit
    pp-number identifier-nondigit
    pp-number ''' digit
    pp-number ''' nondigit
    pp-number 'e' sign
    pp-number 'E' sign
    pp-number 'p' sign
    pp-number 'P' sign
    pp-number '.'
```

```bnf
identifier:
    identifier-nondigit
    identifier identifier-nondigit
    identifier digit
```

```bnf
identifier-nondigit:
    nondigit
    universal-character-name
```

```bnf
nondigit: one of
    'a b c d e f g h i j k l m'
    'n o p q r s t u v w x y z'
    'A B C D E F G H I J K L M'
    'N O P Q R S T U V W X Y Z _'
```

```bnf
digit: one of
    '0 1 2 3 4 5 6 7 8 9'
```

```bnf
keyword:
    any identifier listed in Table 5
    import-keyword
    module-keyword
    export-keyword
```

```bnf
preprocessing-op-or-punc:
    preprocessing-operator
    operator-or-punctuator
```

```bnf
preprocessing-operator: one of
    '# ## %: %:%:'
```

```bnf
operator-or-punctuator: one of
    '{ } [ ] ( )'
    '<: :> <% %>;: ...'
    '? ::. .* -> ->* ~'
    '! + - * / % ^ & |'
    '= += -= *= /= %= ^= &= |='
    '== != < > <= >= <=> && ||'
    '<< >> <<= >>= ++ --,'
    'and or xor not bitand bitor compl'
    'and_eq or_eq xor_eq not_eq'
```

```bnf
literal:
    integer-literal
    character-literal
    floating-point-literal
    string-literal
    boolean-literal
    pointer-literal
    user-defined-literal
```

```bnf
integer-literal:
    binary-literal integer-suffixₒₚₜ
    octal-literal integer-suffixₒₚₜ
    decimal-literal integer-suffixₒₚₜ
    hexadecimal-literal integer-suffixₒₚₜ
```

```bnf
binary-literal:
    '0b' binary-digit
    '0B' binary-digit
    binary-literal '''ₒₚₜ binary-digit
```

```bnf
octal-literal:
    '0'
    octal-literal '''ₒₚₜ octal-digit
```

```bnf
decimal-literal:
    nonzero-digit
    decimal-literal '''ₒₚₜ digit
```

```bnf
hexadecimal-literal:
    hexadecimal-prefix hexadecimal-digit-sequence
```

```bnf
binary-digit: one of
    '0 1'
```

```bnf
octal-digit: one of
    '0 1 2 3 4 5 6 7'
```

```bnf
nonzero-digit: one of
    '1 2 3 4 5 6 7 8 9'
```

```bnf
hexadecimal-prefix: one of
    '0x 0X'
```

```bnf
hexadecimal-digit-sequence:
    hexadecimal-digit
    hexadecimal-digit-sequence '''ₒₚₜ hexadecimal-digit
```

```bnf
hexadecimal-digit: one of
    '0 1 2 3 4 5 6 7 8 9'
    'a b c d e f'
    'A B C D E F'
```

```bnf
integer-suffix:
    unsigned-suffix long-suffixₒₚₜ
    unsigned-suffix long-long-suffixₒₚₜ
    long-suffix unsigned-suffixₒₚₜ
    long-long-suffix unsigned-suffixₒₚₜ
```

```bnf
unsigned-suffix: one of
    'u U'
```

```bnf
long-suffix: one of
    'l L'
```

```bnf
long-long-suffix: one of
    'll LL'
```

```bnf
character-literal:
    encoding-prefixₒₚₜ ''' c-char-sequence '''
```

```bnf
encoding-prefix: one of
    'u8' 'u' 'U' 'L'
```

```bnf
c-char-sequence:
    c-char
    c-char-sequence c-char
```

```bnf
c-char:
    any member of the basic source character set except the single-quote ''', backslash '\', or new-line character
    escape-sequence
    universal-character-name
```

```bnf
escape-sequence:
    simple-escape-sequence
    octal-escape-sequence
    hexadecimal-escape-sequence
```

```bnf
simple-escape-sequence: one of
    '\'' '\"' '\?' '\\'
    '\a' '\b' '\f' '\n' '\r' '\t' '\v'
```

```bnf
octal-escape-sequence:
    '\' octal-digit
    '\' octal-digit octal-digit
    '\' octal-digit octal-digit octal-digit
```

```bnf
hexadecimal-escape-sequence:
    '\x' hexadecimal-digit
    hexadecimal-escape-sequence hexadecimal-digit
```

```bnf
floating-point-literal:
    decimal-floating-point-literal
    hexadecimal-floating-point-literal
```

```bnf
decimal-floating-point-literal:
    fractional-constant exponent-partₒₚₜ floating-point-suffixₒₚₜ
    digit-sequence exponent-part floating-point-suffixₒₚₜ
```

```bnf
hexadecimal-floating-point-literal:
    hexadecimal-prefix hexadecimal-fractional-constant binary-exponent-part floating-point-suffixₒₚₜ
    hexadecimal-prefix hexadecimal-digit-sequence binary-exponent-part floating-point-suffixₒₚₜ
```

```bnf
fractional-constant:
    digit-sequenceₒₚₜ '.' digit-sequence
    digit-sequence '.'
```

```bnf
hexadecimal-fractional-constant:
    hexadecimal-digit-sequenceₒₚₜ '.' hexadecimal-digit-sequence
    hexadecimal-digit-sequence '.'
```

```bnf
exponent-part:
    'e' signₒₚₜ digit-sequence
    'E' signₒₚₜ digit-sequence
```

```bnf
binary-exponent-part:
    'p' signₒₚₜ digit-sequence
    'P' signₒₚₜ digit-sequence
```

```bnf
sign: one of
    '+ -'
```

```bnf
digit-sequence:
    digit
    digit-sequence '''ₒₚₜ digit
```

```bnf
floating-point-suffix: one of
    'f l F L'
```

```bnf
string-literal:
    encoding-prefixₒₚₜ '"' s-char-sequenceₒₚₜ '"'
    encoding-prefixₒₚₜ 'R' raw-string
```

```bnf
s-char-sequence:
    s-char
    s-char-sequence s-char
```

```bnf
s-char:
    any member of the basic source character set except the double-quote '"', backslash '\', or new-line character
    escape-sequence
    universal-character-name
```

```bnf
raw-string:
    '"' d-char-sequenceₒₚₜ '(' r-char-sequenceₒₚₜ ')' d-char-sequenceₒₚₜ '"'
```

```bnf
r-char-sequence:
    r-char
    r-char-sequence r-char
```

```bnf
r-char:
    any member of the source character set, except a right parenthesis ')' followed by
        the initial d-char-sequence (which may be empty) followed by a double quote '"'.
```

```bnf
d-char-sequence:
    d-char
    d-char-sequence d-char
```

```bnf
d-char:
    any member of the basic source character set except:
        space, the left parenthesis '(', the right parenthesis ')', the backslash '\', and the control characters
        representing horizontal tab, vertical tab, form feed, and newline.
```

```bnf
boolean-literal:
    'false'
    'true'
```

```bnf
pointer-literal:
    'nullptr'
```

```bnf
user-defined-literal:
    user-defined-integer-literal
    user-defined-floating-point-literal
    user-defined-string-literal
    user-defined-character-literal
```

```bnf
user-defined-integer-literal:
    decimal-literal ud-suffix
    octal-literal ud-suffix
    hexadecimal-literal ud-suffix
    binary-literal ud-suffix
```

```bnf
user-defined-floating-point-literal:
    fractional-constant exponent-partₒₚₜ ud-suffix
    digit-sequence exponent-part ud-suffix
    hexadecimal-prefix hexadecimal-fractional-constant binary-exponent-part ud-suffix
    hexadecimal-prefix hexadecimal-digit-sequence binary-exponent-part ud-suffix
```

```bnf
user-defined-string-literal:
    string-literal ud-suffix
```

```bnf
user-defined-character-literal:
    character-literal ud-suffix
```

```bnf
ud-suffix:
    identifier
```
