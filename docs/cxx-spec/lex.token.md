# 5.6 Tokens [lex.token]

[Contents](README.md) · up: [5 Lexical conventions \[lex\]](lex.md) · prev: [5.5 Alternative tokens \[lex.digraph\]](lex.digraph.md) · next: [5.7 Comments \[lex.comment\]](lex.comment.md)

```bnf
token:
    identifier
    keyword
    literal
    operator-or-punctuator
```

**1** There are five kinds of tokens: identifiers, keywords, literals,[^1] operators, and other separators. Blanks, horizontal and vertical tabs, newlines, formfeeds, and comments (collectively, “white space”), as described below, are ignored except as they serve to separate tokens. [*Note 1*: Some white space is required to separate otherwise adjacent identifiers, keywords, numeric literals, and alternative tokens containing alphabetic characters. — *end note*]

[^1]: Literals include strings and character and numeric literals.
