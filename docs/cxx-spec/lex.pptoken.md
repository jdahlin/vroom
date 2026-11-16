# 5.4 Preprocessing tokens [lex.pptoken]

[Contents](README.md) · up: [5 Lexical conventions \[lex\]](lex.md) · prev: [5.3 Character sets \[lex.charset\]](lex.charset.md) · next: [5.5 Alternative tokens \[lex.digraph\]](lex.digraph.md)

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

**1** Each preprocessing token that is converted to a token ([[lex.token]](lex.token.md)) shall have the lexical form of a keyword, an identifier, a literal, or an operator or punctuator.

**2** A preprocessing token is the minimal lexical element of the language in translation phases 3 through 6. The categories of preprocessing token are: header names, placeholder tokens produced by preprocessing `import` and `module` directives (*import-keyword*, *module-keyword*, and *export-keyword*), identifiers, preprocessing numbers, character literals (including user-defined character literals), string literals (including user-defined string literals), preprocessing operators and punctuators, and single non-white-space characters that do not lexically match the other preprocessing token categories. If a `'` or a `"` character matches the last category, the behavior is undefined. Preprocessing tokens can be separated by white space; this consists of comments ([[lex.comment]](lex.comment.md)), or white-space characters (space, horizontal tab, new-line, vertical tab, and form-feed), or both. As described in [[cpp]](cpp.md), in certain circumstances during translation phase 4, white space (or the absence thereof) serves as more than preprocessing token separation. White space can appear within a preprocessing token only as part of a header name or between the quotation characters in a character literal or string literal.

**3** If the input stream has been parsed into preprocessing tokens up to a given character:

- If the next character begins a sequence of characters that could be the prefix and initial double quote of a raw string literal, such as `R"`, the next preprocessing token shall be a raw string literal. Between the initial and final double quote characters of the raw string, any transformations performed in phases 1 and 2 (*universal-character-name*s and line splicing) are reverted; this reversion shall apply before any *d-char*, *r-char*, or delimiting parenthesis is identified. The raw string literal is defined as the shortest sequence of characters that matches the raw-string pattern

  ```bnf
  encoding-prefixₒₚₜ 'R' raw-string
  ```

- Otherwise, if the next three characters are `<::` and the subsequent character is neither `:` nor `>`, the `<` is treated as a preprocessing token by itself and not as the first character of the alternative token `<:`.

- Otherwise, the next preprocessing token is the longest sequence of characters that could constitute a preprocessing token, even if that would cause further lexical analysis to fail, except that a *header-name* ([[lex.header]](lex.header.md)) is only formed

  - after the `include` or `import` preprocessing token in an `#include` ([[cpp.include]](cpp.include.md)) or `import` ([[cpp.import]](cpp.import.md)) directive, or
  - within a *has-include-expression*.

[*Example 1*:

```cpp
#define R "x"
const char* s = R"y";           // ill-formed raw string, not "x" "y"
```

— *end example*]

**4** The *import-keyword* is produced by processing an `import` directive ([[cpp.import]](cpp.import.md)), the *module-keyword* is produced by preprocessing a `module` directive ([[cpp.module]](cpp.module.md)), and the *export-keyword* is produced by preprocessing either of the previous two directives. [*Note 1*: None has any observable spelling. — *end note*]

**5** [*Example 2*: The program fragment `0xe+foo` is parsed as a preprocessing number token (one that is not a valid *integer-literal* or *floating-point-literal* token), even though a parse as three preprocessing tokens `0xe`, `+`, and `foo` might produce a valid expression (for example, if `foo` were a macro defined as `1`). Similarly, the program fragment `1E1` is parsed as a preprocessing number (one that is a valid *floating-point-literal* token), whether or not `E` is a macro name. — *end example*]

**6** [*Example 3*: The program fragment `x+++++y` is parsed as `x ++ ++ + y`, which, if `x` and `y` have integral types, violates a constraint on increment operators, even though the parse `x ++ + ++ y` might yield a correct expression. — *end example*]
