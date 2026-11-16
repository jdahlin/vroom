# 8.2 Labeled statement [stmt.label]

[Contents](../README.md) · up: [8 Statements \[stmt.stmt\]](stmt.stmt.md) · prev: [8.1 Preamble \[stmt.pre\]](stmt.pre.md) · next: [8.3 Expression statement \[stmt.expr\]](stmt.expr.md)

**1** A statement can be labeled.

```bnf
labeled-statement:
    attribute-specifier-seqₒₚₜ identifier ':' statement
    attribute-specifier-seqₒₚₜ 'case' constant-expression ':' statement
    attribute-specifier-seqₒₚₜ 'default' ':' statement
```

The optional *attribute-specifier-seq* appertains to the label. An *identifier label* declares the identifier. The only use of an identifier label is as the target of a `goto`. The scope of a label is the function in which it appears. Labels shall not be redeclared within a function. A label can be used in a `goto` statement before its declaration. Labels have their own name space and do not interfere with other identifiers. [*Note 1*: A label may have the same name as another declaration in the same scope or a *template-parameter* from an enclosing scope. Unqualified name lookup ([[basic.lookup.unqual]](../basic/basic.lookup.unqual.md)) ignores labels. — *end note*]

**2** Case labels and default labels shall occur only in `switch` statements.
