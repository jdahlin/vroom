# 4.3 Syntax notation [syntax]

[Contents](README.md) · up: [4 General principles \[intro\]](intro.md) · prev: [4.2 Structure of this document \[intro.structure\]](intro.structure.md) · next: [4.4 Acknowledgments \[intro.ack\]](intro.ack.md)

**1** In the syntax notation used in this document, syntactic categories are indicated by *italic* type, and literal words and characters in `constant` `width` type. Alternatives are listed on separate lines except in a few cases where a long set of alternatives is marked by the phrase “one of”. If the text of an alternative is too long to fit on a line, the text is continued on subsequent lines indented from the first one. An optional terminal or non-terminal symbol is indicated by the subscript “ₒₚₜ”, so

```bnf
'{' expressionₒₚₜ '}'
```

indicates an optional expression enclosed in braces.

**2** Names for syntactic categories have generally been chosen according to the following rules:

- *X-name* is a use of an identifier in a context that determines its meaning (e.g., *class-name*, *typedef-name*).
- *X-id* is an identifier with no context-dependent meaning (e.g., *qualified-id*).
- *X-seq* is one or more *X*’s without intervening delimiters (e.g., *declaration-seq* is a sequence of declarations).
- *X-list* is one or more *X*’s separated by intervening commas (e.g., *identifier-list* is a sequence of identifiers separated by commas).
