# 5.8 Header names [lex.header]

[Contents](../README.md) · up: [5 Lexical conventions \[lex\]](lex.md) · prev: [5.7 Comments \[lex.comment\]](lex.comment.md) · next: [5.9 Preprocessing numbers \[lex.ppnumber\]](lex.ppnumber.md)

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

**1** [*Note 1*: Header name preprocessing tokens only appear within a `#include` preprocessing directive, a `__has_include` preprocessing expression, or after certain occurrences of an `import` token (see [[lex.pptoken]](lex.pptoken.md)). — *end note*] The sequences in both forms of *header-name*s are mapped in an implementation-defined manner to headers or to external source file names as specified in [[cpp.include]](../cpp/cpp.include.md).

**2** The appearance of either of the characters `'` or `\` or of either of the character sequences `/*` or `//` in a *q-char-sequence* or an *h-char-sequence* is conditionally-supported with implementation-defined semantics, as is the appearance of the character `"` in an *h-char-sequence*.[^1]

[^1]: Thus, a sequence of characters that resembles an escape sequence might result in an error, be interpreted as the character corresponding to the escape sequence, or have a completely different meaning, depending on the implementation.
