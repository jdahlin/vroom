# 9.1 Preamble [dcl.pre]

[Contents](../README.md) · up: [9 Declarations \[dcl.dcl\]](dcl.dcl.md) · prev: [9 Declarations \[dcl.dcl\]](dcl.dcl.md) · next: [9.2 Specifiers \[dcl.spec\]](dcl.spec.md)

**1** Declarations generally specify how names are to be interpreted. Declarations have the form

```bnf
declaration-seq:
    declaration
    declaration-seq declaration
```

```bnf
declaration:
    block-declaration
    nodeclspec-function-declaration
    function-definition
    template-declaration
    deduction-guide
    explicit-instantiation
    explicit-specialization
    export-declaration
    linkage-specification
    namespace-definition
    empty-declaration
    attribute-declaration
    module-import-declaration
```

```bnf
block-declaration:
    simple-declaration
    asm-declaration
    namespace-alias-definition
    using-declaration
    using-enum-declaration
    using-directive
    static_assert-declaration
    alias-declaration
    opaque-enum-declaration
```

```bnf
nodeclspec-function-declaration:
    attribute-specifier-seqₒₚₜ declarator ';'
```

```bnf
alias-declaration:
    'using' identifier attribute-specifier-seqₒₚₜ '=' defining-type-id ';'
```

```bnf
simple-declaration:
    decl-specifier-seq init-declarator-listₒₚₜ ';'
    attribute-specifier-seq decl-specifier-seq init-declarator-list ';'
    attribute-specifier-seqₒₚₜ decl-specifier-seq ref-qualifierₒₚₜ '[' identifier-list ']' initializer ';'
```

```bnf
static_assert-declaration:
  'static_assert' '(' constant-expression ')' ';'
  'static_assert' '(' constant-expression ',' string-literal ')' ';'
```

```bnf
empty-declaration:
    ';'
```

```bnf
attribute-declaration:
    attribute-specifier-seq ';'
```

[*Note 1*: *asm-declaration*s are described in [[dcl.asm]](dcl.asm.md), and *linkage-specification*s are described in [[dcl.link]](dcl.link.md); *function-definition*s are described in [[dcl.fct.def]](dcl.fct.def.md) and *template-declaration*s and *deduction-guide*s are described in [[temp.deduct.guide]](../temp/temp.deduct.guide.md); *namespace-definition*s are described in [[namespace.def]](namespace.def.md), *using-declaration*s are described in [[namespace.udecl]](namespace.udecl.md) and *using-directive*s are described in [[namespace.udir]](namespace.udir.md). — *end note*]

**2** A *simple-declaration* or *nodeclspec-function-declaration* of the form

```bnf
attribute-specifier-seqₒₚₜ decl-specifier-seqₒₚₜ init-declarator-listₒₚₜ ';'
```

is divided into three parts. Attributes are described in [[dcl.attr]](dcl.attr.md). *decl-specifier*s, the principal components of a *decl-specifier-seq*, are described in [[dcl.spec]](dcl.spec.md). *declarator*s, the components of an *init-declarator-list*, are described in [[dcl.decl]](dcl.decl.md). The *attribute-specifier-seq* appertains to each of the entities declared by the *declarator*s of the *init-declarator-list*. [*Note 2*: In the declaration for an entity, attributes appertaining to that entity may appear at the start of the declaration and after the *declarator-id* for that declaration. — *end note*] [*Example 1*:

```cpp
[[noreturn]] void f [[noreturn]] ();    // OK
```

— *end example*]

**3** Except where otherwise specified, the meaning of an *attribute-declaration* is implementation-defined.

**4** A declaration occurs in a scope ([[basic.scope]](../basic/basic.scope.md)); the scope rules are summarized in [[basic.lookup]](../basic/basic.lookup.md). A declaration that declares a function or defines a class, namespace, template, or function also has one or more scopes nested within it. These nested scopes, in turn, can have declarations nested within them. Unless otherwise stated, utterances in [[dcl.dcl]](dcl.dcl.md) about components in, of, or contained by a declaration or subcomponent thereof refer only to those components of the declaration that are *not* nested within scopes nested within the declaration.

**5** In a *simple-declaration*, the optional *init-declarator-list* can be omitted only when declaring a class ([[class]](../class/class.md)) or enumeration ([[dcl.enum]](dcl.enum.md)), that is, when the *decl-specifier-seq* contains either a *class-specifier*, an *elaborated-type-specifier* with a *class-key* ([[class.name]](../class/class.name.md)), or an *enum-specifier*. In these cases and whenever a *class-specifier* or *enum-specifier* is present in the *decl-specifier-seq*, the identifiers in these specifiers are among the names being declared by the declaration (as *class-name*s, *enum-name*s, or *enumerator*s, depending on the syntax). In such cases, the *decl-specifier-seq* shall introduce one or more names into the program, or shall redeclare a name introduced by a previous declaration. [*Example 2*:

```cpp
enum { };           // error
typedef class { };  // error
```

— *end example*]

**6** In a *static\_assert-declaration*, the *constant-expression* shall be a contextually converted constant expression of type `bool` ([[expr.const]](../expr/expr.const.md)). If the value of the expression when so converted is `true`, the declaration has no effect. Otherwise, the program is ill-formed, and the resulting diagnostic message ([[intro.compliance]](../intro/intro.compliance.md)) shall include the text of the *string-literal*, if one is supplied, except that characters not in the basic source character set ([[lex.charset]](../lex/lex.charset.md)) are not required to appear in the diagnostic message. [*Example 3*:

```cpp
static_assert(sizeof(int) == sizeof(void*), "wrong pointer size");
```

— *end example*]

**7** An *empty-declaration* has no effect.

**8** A *simple-declaration* with an *identifier-list* is called a *structured binding declaration* ([[dcl.struct.bind]](dcl.struct.bind.md)). If the *decl-specifier-seq* contains any *decl-specifier* other than `static`, `thread_local`, `auto` ([[dcl.spec.auto]](dcl.spec.auto.md)), or *cv-qualifier*s, the program is ill-formed. The *initializer* shall be of the form “`=` *assignment-expression*”, of the form “`{` *assignment-expression* `}`”, or of the form “`(` *assignment-expression* `)`”, where the *assignment-expression* is of array or non-union class type.

**9** Each *init-declarator* in the *init-declarator-list* contains exactly one *declarator-id*, which is the name declared by that *init-declarator* and hence one of the names declared by the declaration. The *defining-type-specifier*s ([[dcl.type]](dcl.type.md)) in the *decl-specifier-seq* and the recursive *declarator* structure of the *init-declarator* describe a type ([[dcl.meaning]](dcl.meaning.md)), which is then associated with the name being declared by the *init-declarator*.

**10** If the *decl-specifier-seq* contains the `typedef` specifier, the declaration is called a *typedef declaration* and the name of each *init-declarator* is declared to be a *typedef-name*, synonymous with its associated type ([[dcl.typedef]](dcl.typedef.md)). If the *decl-specifier-seq* contains no `typedef` specifier, the declaration is called a *function declaration* if the type associated with the name is a function type ([[dcl.fct]](dcl.fct.md)) and an *object declaration* otherwise.

**11** Syntactic components beyond those found in the general form of declaration are added to a function declaration to make a *function-definition*. An object declaration, however, is also a definition unless it contains the `extern` specifier and has no initializer ([[basic.def]](../basic/basic.def.md)). An object definition causes storage of appropriate size and alignment to be reserved and any appropriate initialization ([[dcl.init]](dcl.init.md)) to be done.

**12** A *nodeclspec-function-declaration* shall declare a constructor, destructor, or conversion function. [*Note 3*: A *nodeclspec-function-declaration* can only be used in a *template-declaration* ([[temp.pre]](../temp/temp.pre.md)), *explicit-instantiation* ([[temp.explicit]](../temp/temp.explicit.md)), or *explicit-specialization* ([[temp.expl.spec]](../temp/temp.expl.spec.md)). — *end note*]
