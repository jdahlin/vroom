# 9.12.1 Attribute syntax and semantics [dcl.attr.grammar]

[Contents](../README.md) · up: [9.12 Attributes \[dcl.attr\]](dcl.attr.md) · prev: [9.12 Attributes \[dcl.attr\]](dcl.attr.md) · next: [9.12.2 Alignment specifier \[dcl.align\]](dcl.align.md)

**1** Attributes specify additional information for various source constructs such as types, variables, names, blocks, or translation units.

```bnf
attribute-specifier-seq:
  attribute-specifier-seqₒₚₜ attribute-specifier
```

```bnf
attribute-specifier:
  '[' '[' attribute-using-prefixₒₚₜ attribute-list ']' ']'
  alignment-specifier
```

```bnf
alignment-specifier:
  'alignas' '(' type-id '...'ₒₚₜ ')'
  'alignas' '(' constant-expression '...'ₒₚₜ ')'
```

```bnf
attribute-using-prefix:
  'using' attribute-namespace ':'
```

```bnf
attribute-list:
  attributeₒₚₜ
  attribute-list ',' attributeₒₚₜ
  attribute '...'
  attribute-list ',' attribute '...'
```

```bnf
attribute:
    attribute-token attribute-argument-clauseₒₚₜ
```

```bnf
attribute-token:
    identifier
    attribute-scoped-token
```

```bnf
attribute-scoped-token:
    attribute-namespace '::' identifier
```

```bnf
attribute-namespace:
    identifier
```

```bnf
attribute-argument-clause:
    '(' balanced-token-seqₒₚₜ ')'
```

```bnf
balanced-token-seq:
    balanced-token
    balanced-token-seq balanced-token
```

```bnf
balanced-token:
    '(' balanced-token-seqₒₚₜ ')'
    '[' balanced-token-seqₒₚₜ ']'
    '{' balanced-token-seqₒₚₜ '}'
    any token other than a parenthesis, a bracket, or a brace
```

**2** If an *attribute-specifier* contains an *attribute-using-prefix*, the *attribute-list* following that *attribute-using-prefix* shall not contain an *attribute-scoped-token* and every *attribute-token* in that *attribute-list* is treated as if its *identifier* were prefixed with `N::`, where `N` is the *attribute-namespace* specified in the *attribute-using-prefix*. [*Note 1*: This rule imposes no constraints on how an *attribute-using-prefix* affects the tokens in an *attribute-argument-clause*. — *end note*] [*Example 1*:

```cpp
[[using CC: opt(1), debug]]         // same as [[CC::opt(1), CC::debug]]
  void f() {}
[[using CC: opt(1)]] [[CC::debug]]  // same as [[CC::opt(1)]] [[CC::debug]]
  void g() {}
[[using CC: CC::opt(1)]]            // error: cannot combine using and scoped attribute token
  void h() {}
```

— *end example*]

**3** [*Note 2*: For each individual attribute, the form of the *balanced-token-seq* will be specified. — *end note*]

**4** In an *attribute-list*, an ellipsis may appear only if that *attribute*’s specification permits it. An *attribute* followed by an ellipsis is a pack expansion ([[temp.variadic]](../temp/temp.variadic.md)). An *attribute-specifier* that contains no *attribute*s has no effect. The order in which the *attribute-token*s appear in an *attribute-list* is not significant. If a keyword ([[lex.key]](../lex/lex.key.md)) or an alternative token ([[lex.digraph]](../lex/lex.digraph.md)) that satisfies the syntactic requirements of an *identifier* ([[lex.name]](../lex/lex.name.md)) is contained in an *attribute-token*, it is considered an identifier. No name lookup ([[basic.lookup]](../basic/basic.lookup.md)) is performed on any of the identifiers contained in an *attribute-token*. The *attribute-token* determines additional requirements on the *attribute-argument-clause* (if any).

**5** Each *attribute-specifier-seq* is said to *appertain* to some entity or statement, identified by the syntactic context where it appears ([[stmt.stmt]](../stmt/stmt.stmt.md), [[dcl.dcl]](dcl.dcl.md), [[dcl.decl]](dcl.decl.md)). If an *attribute-specifier-seq* that appertains to some entity or statement contains an *attribute* or *alignment-specifier* that is not allowed to apply to that entity or statement, the program is ill-formed. If an *attribute-specifier-seq* appertains to a friend declaration ([[class.friend]](../class/class.friend.md)), that declaration shall be a definition. [*Note 3*: An *attribute-specifier-seq* cannot appeartain to an explicit instantiation ([[temp.explicit]](../temp/temp.explicit.md)). — *end note*]

**6** For an *attribute-token* (including an *attribute-scoped-token*) not specified in this document, the behavior is implementation-defined. Any *attribute-token* that is not recognized by the implementation is ignored. An *attribute-token* is reserved for future standardization if

- it is not an *attribute-scoped-token* and is not specified in this document, or
- it is an *attribute-scoped-token* and its *attribute-namespace* is `std` followed by zero or more digits.

[*Note 4*: Each implementation should choose a distinctive name for the *attribute-namespace* in an *attribute-scoped-token*. — *end note*]

**7** Two consecutive left square bracket tokens shall appear only when introducing an *attribute-specifier* or within the *balanced-token-seq* of an *attribute-argument-clause*. [*Note 5*: If two consecutive left square brackets appear where an *attribute-specifier* is not allowed, the program is ill-formed even if the brackets match an alternative grammar production. — *end note*] [*Example 2*:

```cpp
int p[10];
void f() {
  int x = 42, y[5];
  int(p[[x] { return x; }()]);  // error: invalid attribute on a nested declarator-id and
                                // not a function-style cast of an element of p.
  y[[] { return 2; }()] = 2;    // error even though attributes are not allowed in this context.
  int i [[vendor::attr([[]])]]; // well-formed implementation-defined attribute.
}
```

— *end example*]
