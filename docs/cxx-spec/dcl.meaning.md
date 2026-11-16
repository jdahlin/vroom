# 9.3.3 Meaning of declarators [dcl.meaning]

[Contents](README.md) · up: [9.3 Declarators \[dcl.decl\]](dcl.decl.md) · prev: [9.3.2 Ambiguity resolution \[dcl.ambig.res\]](dcl.ambig.res.md) · next: [9.3.3.1 Pointers \[dcl.ptr\]](dcl.ptr.md)

**1** A declarator contains exactly one *declarator-id*; it names the identifier that is declared. An *unqualified-id* occurring in a *declarator-id* shall be a simple *identifier* except for the declaration of some special functions ([[class.ctor]](class.ctor.md), [[class.conv]](class.conv.md), [[class.dtor]](class.dtor.md), [[over.oper]](over.oper.md)) and for the declaration of template specializations or partial specializations ([[temp.spec]](temp.spec.md)). When the *declarator-id* is qualified, the declaration shall refer to a previously declared member of the class or namespace to which the qualifier refers (or, in the case of a namespace, of an element of the inline namespace set of that namespace ([[namespace.def]](namespace.def.md))) or to a specialization thereof; the member shall not merely have been introduced by a *using-declaration* in the scope of the class or namespace nominated by the *nested-name-specifier* of the *declarator-id*. The *nested-name-specifier* of a qualified *declarator-id* shall not begin with a *decltype-specifier*. [*Note 1*: If the qualifier is the global `::` scope resolution operator, the *declarator-id* refers to a name declared in the global namespace scope. — *end note*] The optional *attribute-specifier-seq* following a *declarator-id* appertains to the entity that is declared.

**2** A `static`, `thread_local`, `extern`, `mutable`, `friend`, `inline`, `virtual`, `constexpr`, or `typedef` specifier or an *explicit-specifier* applies directly to each *declarator-id* in an *init-declarator-list* or *member-declarator-list*; the type specified for each *declarator-id* depends on both the *decl-specifier-seq* and its *declarator*.

**3** Thus, a declaration of a particular identifier has the form

```cpp
T D
```

where `T` is of the form *attribute-specifier-seq*ₒₚₜ *decl-specifier-seq* and `D` is a declarator. Following is a recursive procedure for determining the type specified for the contained *declarator-id* by such a declaration.

**4** First, the *decl-specifier-seq* determines a type. In a declaration

```cpp
T D
```

the *decl-specifier-seq* `T` determines the type `T`. [*Example 1*: In the declaration

```cpp
int unsigned i;
```

the type specifiers `int` `unsigned` determine the type “`unsigned int`” ([[dcl.type.simple]](dcl.type.simple.md)). — *end example*]

**5** In a declaration *attribute-specifier-seq*ₒₚₜ `T` `D` where `D` is an unadorned identifier the type of this identifier is “`T`”.

**6** In a declaration `T` `D` where `D` has the form

```bnf
'(' 'D1' ')'
```

the type of the contained *declarator-id* is the same as that of the contained *declarator-id* in the declaration

```cpp
T D1
```

Parentheses do not alter the type of the embedded *declarator-id*, but they can alter the binding of complex declarators.

## Subclauses

- [9.3.3.1 Pointers \[dcl.ptr\]](dcl.ptr.md)
- [9.3.3.2 References \[dcl.ref\]](dcl.ref.md)
- [9.3.3.3 Pointers to members \[dcl.mptr\]](dcl.mptr.md)
- [9.3.3.4 Arrays \[dcl.array\]](dcl.array.md)
- [9.3.3.5 Functions \[dcl.fct\]](dcl.fct.md)
- [9.3.3.6 Default arguments \[dcl.fct.default\]](dcl.fct.default.md)
