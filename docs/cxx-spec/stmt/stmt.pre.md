# 8.1 Preamble [stmt.pre]

[Contents](../README.md) · up: [8 Statements \[stmt.stmt\]](stmt.stmt.md) · prev: [8 Statements \[stmt.stmt\]](stmt.stmt.md) · next: [8.2 Labeled statement \[stmt.label\]](stmt.label.md)

**1** Except as indicated, statements are executed in sequence.

```bnf
statement:
    labeled-statement
    attribute-specifier-seqₒₚₜ expression-statement
    attribute-specifier-seqₒₚₜ compound-statement
    attribute-specifier-seqₒₚₜ selection-statement
    attribute-specifier-seqₒₚₜ iteration-statement
    attribute-specifier-seqₒₚₜ jump-statement
    declaration-statement
    attribute-specifier-seqₒₚₜ try-block

init-statement:
    expression-statement
    simple-declaration

condition:
    expression
    attribute-specifier-seqₒₚₜ decl-specifier-seq declarator brace-or-equal-initializer
```

The optional *attribute-specifier-seq* appertains to the respective statement.

**2** A *substatement* of a *statement* is one of the following:

- for a *labeled-statement*, its contained *statement*,
- for a *compound-statement*, any *statement* of its *statement-seq*,
- for a *selection-statement*, any of its *statement*s (but not its *init-statement*), or
- for an *iteration-statement*, its contained *statement* (but not an *init-statement*).

[*Note 1*: The *compound-statement* of a *lambda-expression* is not a substatement of the *statement* (if any) in which the *lambda-expression* lexically appears. — *end note*]

**3** A *statement* `S1` *encloses* a *statement* `S2` if

- `S2` is a substatement of `S1` ([[dcl.dcl]](../dcl/dcl.dcl.md)),
- `S1` is a *selection-statement* or *iteration-statement* and `S2` is the *init-statement* of `S1`,
- `S1` is a *try-block* and `S2` is its *compound-statement* or any of the *compound-statement*s of its *handler*s, or
- `S1` encloses a statement `S3` and `S3` encloses `S2`.

**4** The rules for *condition*s apply both to *selection-statement*s and to the `for` and `while` statements ([[stmt.iter]](stmt.iter.md)). A *condition* that is not an *expression* is a declaration ([[dcl.dcl]](../dcl/dcl.dcl.md)). The *declarator* shall not specify a function or an array. The *decl-specifier-seq* shall not define a class or enumeration. If the `auto` *type-specifier* appears in the *decl-specifier-seq*, the type of the identifier being declared is deduced from the initializer as described in [[dcl.spec.auto]](../dcl/dcl.spec.auto.md).

**5** [*Note 2*: A name introduced in a *selection-statement* or *iteration-statement* outside of any substatement is in scope from its point of declaration until the end of the statement’s substatements. Such a name cannot be redeclared in the outermost block of any of the substatements ([[basic.scope.block]](../basic/basic.scope.block.md)). — *end note*]

**6** The value of a *condition* that is an initialized declaration in a statement other than a `switch` statement is the value of the declared variable contextually converted to `bool` ([[conv]](../expr/conv.md)). If that conversion is ill-formed, the program is ill-formed. The value of a *condition* that is an initialized declaration in a `switch` statement is the value of the declared variable if it has integral or enumeration type, or of that variable implicitly converted to integral or enumeration type otherwise. The value of a *condition* that is an expression is the value of the expression, contextually converted to `bool` for statements other than `switch`; if that conversion is ill-formed, the program is ill-formed. The value of the condition will be referred to as simply “the condition” where the usage is unambiguous.

**7** If a *condition* can be syntactically resolved as either an expression or the declaration of a block-scope name, it is interpreted as a declaration.

**8** In the *decl-specifier-seq* of a *condition*, each *decl-specifier* shall be either a *type-specifier* or `constexpr`.
