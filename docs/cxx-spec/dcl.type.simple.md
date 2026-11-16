# 9.2.8.2 Simple type specifiers [dcl.type.simple]

[Contents](README.md) · up: [9.2.8 Type specifiers \[dcl.type\]](dcl.type.md) · prev: [9.2.8.1 The *cv-qualifier*s \[dcl.type.cv\]](dcl.type.cv.md) · next: [9.2.8.3 Elaborated type specifiers \[dcl.type.elab\]](dcl.type.elab.md)

**1** The simple type specifiers are

```bnf
simple-type-specifier:
    nested-name-specifierₒₚₜ type-name
    nested-name-specifier 'template' simple-template-id
    decltype-specifier
    placeholder-type-specifier
    nested-name-specifierₒₚₜ template-name
    'char'
    'char8_t'
    'char16_t'
    'char32_t'
    'wchar_t'
    'bool'
    'short'
    'int'
    'long'
    'signed'
    'unsigned'
    'float'
    'double'
    'void'
```

```bnf
type-name:
    class-name
    enum-name
    typedef-name
```

**2** A *placeholder-type-specifier* is a placeholder for a type to be deduced ([[dcl.spec.auto]](dcl.spec.auto.md)). A *type-specifier* of the form `typename`ₒₚₜ *nested-name-specifier*ₒₚₜ *template-name* is a placeholder for a deduced class type ([[dcl.type.class.deduct]](dcl.type.class.deduct.md)). The *nested-name-specifier*, if any, shall be non-dependent and the *template-name* shall name a deducible template. A *deducible template* is either a class template or is an alias template whose *defining-type-id* is of the form

```bnf
'typename'ₒₚₜ nested-name-specifierₒₚₜ 'template'ₒₚₜ simple-template-id
```

where the *nested-name-specifier* (if any) is non-dependent and the *template-name* of the *simple-template-id* names a deducible template. [*Note 1*: An injected-class-name is never interpreted as a *template-name* in contexts where class template argument deduction would be performed ([[temp.local]](temp.local.md)). — *end note*] The other *simple-type-specifier*s specify either a previously-declared type, a type determined from an expression, or one of the fundamental types ([[basic.fundamental]](basic.fundamental.md)). [Table 14](dcl.type.simple.md#tab:dcl.type.simple) summarizes the valid combinations of *simple-type-specifier*s and the types they specify.

<a id="tab:dcl.type.simple"></a>
**Table 14: *simple-type-specifier*s and the types they specify** [tab:dcl.type.simple]

| Specifier(s) | Type |
| --- | --- |
| *type-name* | the type named |
| *simple-template-id* | the type as defined in [[temp.names]](temp.names.md) |
| *decltype-specifier* | the type as defined in [[dcl.type.decltype]](dcl.type.decltype.md) |
| *placeholder-type-specifier* | the type as defined in [[dcl.spec.auto]](dcl.spec.auto.md) |
| *template-name* | the type as defined in [[dcl.type.class.deduct]](dcl.type.class.deduct.md) |
| `char` | “`char`” |
| `unsigned char` | “`unsigned char`” |
| `signed char` | “`signed char`” |
| `char8_t` | “`char8_t`” |
| `char16_t` | “`char16_t`” |
| `char32_t` | “`char32_t`” |
| `bool` | “`bool`” |
| `unsigned` | “`unsigned int`” |
| `unsigned int` | “`unsigned int`” |
| `signed` | “`int`” |
| `signed int` | “`int`” |
| `int` | “`int`” |
| `unsigned short int` | “`unsigned short int`” |
| `unsigned short` | “`unsigned short int`” |
| `unsigned long int` | “`unsigned long int`” |
| `unsigned long` | “`unsigned long int`” |
| `unsigned long long int` | “`unsigned long long int`” |
| `unsigned long long` | “`unsigned long long int`” |
| `signed long int` | “`long int`” |
| `signed long` | “`long int`” |
| `signed long long int` | “`long long int`” |
| `signed long long` | “`long long int`” |
| `long long int` | “`long long int`” |
| `long long` | “`long long int`” |
| `long int` | “`long int`” |
| `long` | “`long int`” |
| `signed short int` | “`short int`” |
| `signed short` | “`short int`” |
| `short int` | “`short int`” |
| `short` | “`short int`” |
| `wchar_t` | “`wchar_t`” |
| `float` | “`float`” |
| `double` | “`double`” |
| `long double` | “`long double`” |
| `void` | “`void`” |

**3** When multiple *simple-type-specifier*s are allowed, they can be freely intermixed with other *decl-specifier*s in any order. [*Note 2*: It is implementation-defined whether objects of `char` type are represented as signed or unsigned quantities. The `signed` specifier forces `char` objects to be signed; it is redundant in other contexts. — *end note*]
