# 5.7 Comments [lex.comment]

[Contents](../README.md) · up: [5 Lexical conventions \[lex\]](lex.md) · prev: [5.6 Tokens \[lex.token\]](lex.token.md) · next: [5.8 Header names \[lex.header\]](lex.header.md)

**1** The characters `/*` start a comment, which terminates with the characters `*/`. These comments do not nest. The characters `//` start a comment, which terminates immediately before the next new-line character. If there is a form-feed or a vertical-tab character in such a comment, only white-space characters shall appear between it and the new-line that terminates the comment; no diagnostic is required. [*Note 1*: The comment characters `//`, `/*`, and `*/` have no special meaning within a `//` comment and are treated just like other characters. Similarly, the comment characters `//` and `/*` have no special meaning within a `/*` comment. — *end note*]
