# 9.2.8 Type specifiers [dcl.type]

[Contents](../README.md) · up: [9.2 Specifiers \[dcl.spec\]](dcl.spec.md) · prev: [9.2.7 The `inline` specifier \[dcl.inline\]](dcl.inline.md) · next: [9.2.8.1 The *cv-qualifier*s \[dcl.type.cv\]](dcl.type.cv.md)

**1** The type-specifiers are

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

The optional *attribute-specifier-seq* in a *type-specifier-seq* or a *defining-type-specifier-seq* appertains to the type denoted by the preceding *type-specifier*s or *defining-type-specifier*s ([[dcl.meaning]](dcl.meaning.md)). The *attribute-specifier-seq* affects the type only for the declaration it appears in, not other declarations involving the same type.

**2** As a general rule, at most one *defining-type-specifier* is allowed in the complete *decl-specifier-seq* of a *declaration* or in a *defining-type-specifier-seq*, and at most one *type-specifier* is allowed in a *type-specifier-seq*. The only exceptions to this rule are the following:

- `const` can be combined with any type specifier except itself.
- `volatile` can be combined with any type specifier except itself.
- `signed` or `unsigned` can be combined with `char`, `long`, `short`, or `int`.
- `short` or `long` can be combined with `int`.
- `long` can be combined with `double`.
- `long` can be combined with `long`.

**3** Except in a declaration of a constructor, destructor, or conversion function, at least one *defining-type-specifier* that is not a *cv-qualifier* shall appear in a complete *type-specifier-seq* or a complete *decl-specifier-seq*.[^1]

**4** [*Note 1*: *enum-specifier*s, *class-specifier*s, and *typename-specifier*s are discussed in [[dcl.enum]](dcl.enum.md), [[class]](../class/class.md), and [[temp.res]](../temp/temp.res.md), respectively. The remaining *type-specifier*s are discussed in the rest of this subclause. — *end note*]

## Subclauses

- [9.2.8.1 The *cv-qualifier*s \[dcl.type.cv\]](dcl.type.cv.md)
- [9.2.8.2 Simple type specifiers \[dcl.type.simple\]](dcl.type.simple.md)
- [9.2.8.3 Elaborated type specifiers \[dcl.type.elab\]](dcl.type.elab.md)
- [9.2.8.4 Decltype specifiers \[dcl.type.decltype\]](dcl.type.decltype.md)
- [9.2.8.5 Placeholder type specifiers \[dcl.spec.auto\]](dcl.spec.auto.md)
  - [9.2.8.5.1 Placeholder type deduction \[dcl.type.auto.deduct\]](dcl.type.auto.deduct.md)
- [9.2.8.6 Deduced class template specialization types \[dcl.type.class.deduct\]](dcl.type.class.deduct.md)

[^1]: There is no special provision for a *decl-specifier-seq* that lacks a *type-specifier* or that has a *type-specifier* that only specifies *cv-qualifier*s. The “implicit int” rule of C is no longer supported.
