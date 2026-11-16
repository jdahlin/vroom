# 9.10 The `asm` declaration [dcl.asm]

[Contents](../README.md) · up: [9 Declarations \[dcl.dcl\]](dcl.dcl.md) · prev: [9.9 The `using` declaration \[namespace.udecl\]](namespace.udecl.md) · next: [9.11 Linkage specifications \[dcl.link\]](dcl.link.md)

**1** An `asm` declaration has the form

```bnf
asm-declaration:
    attribute-specifier-seqₒₚₜ 'asm' '(' string-literal ')' ';'
```

The `asm` declaration is conditionally-supported; its meaning is implementation-defined. The optional *attribute-specifier-seq* in an *asm-declaration* appertains to the `asm` declaration. [*Note 1*: Typically it is used to pass information through the implementation to an assembler. — *end note*]
