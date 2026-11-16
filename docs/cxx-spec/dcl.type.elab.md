# 9.2.8.3 Elaborated type specifiers [dcl.type.elab]

[Contents](README.md) · up: [9.2.8 Type specifiers \[dcl.type\]](dcl.type.md) · prev: [9.2.8.2 Simple type specifiers \[dcl.type.simple\]](dcl.type.simple.md) · next: [9.2.8.4 Decltype specifiers \[dcl.type.decltype\]](dcl.type.decltype.md)

```bnf
elaborated-type-specifier:
    class-key attribute-specifier-seqₒₚₜ nested-name-specifierₒₚₜ identifier
    class-key simple-template-id
    class-key nested-name-specifier 'template'ₒₚₜ simple-template-id
    elaborated-enum-specifier
```

```bnf
elaborated-enum-specifier:
    'enum' nested-name-specifierₒₚₜ identifier
```

**1** An *attribute-specifier-seq* shall not appear in an *elaborated-type-specifier* unless the latter is the sole constituent of a declaration. If an *elaborated-type-specifier* is the sole constituent of a declaration, the declaration is ill-formed unless it is an explicit specialization ([[temp.expl.spec]](temp.expl.spec.md)), an explicit instantiation ([[temp.explicit]](temp.explicit.md)) or it has one of the following forms:

```bnf
class-key attribute-specifier-seqₒₚₜ identifier ';'
'friend' class-key '::ₒₚₜ' identifier ';'
'friend' class-key '::ₒₚₜ' simple-template-id ';'
'friend' class-key nested-name-specifier identifier ';'
'friend' class-key nested-name-specifier 'template'ₒₚₜ simple-template-id ';'
```

In the first case, the *attribute-specifier-seq*, if any, appertains to the class being declared; the attributes in the *attribute-specifier-seq* are thereafter considered attributes of the class whenever it is named.

**2** [*Note 1*: [[basic.lookup.elab]](basic.lookup.elab.md) describes how name lookup proceeds for the *identifier* in an *elaborated-type-specifier*. — *end note*] If the *identifier* or *simple-template-id* resolves to a *class-name* or *enum-name*, the *elaborated-type-specifier* introduces it into the declaration the same way a *simple-type-specifier* introduces its *type-name* ([[dcl.type.simple]](dcl.type.simple.md)). If the *identifier* or *simple-template-id* resolves to a *typedef-name* ([[dcl.typedef]](dcl.typedef.md), [[temp.names]](temp.names.md)), the *elaborated-type-specifier* is ill-formed. [*Note 2*: This implies that, within a class template with a template *type-parameter* `T`, the declaration

```cpp
friend class T;
```

is ill-formed. However, the similar declaration `friend T;` is allowed ([[class.friend]](class.friend.md)). — *end note*]

**3** The *class-key* or `enum` keyword present in the *elaborated-type-specifier* shall agree in kind with the declaration to which the name in the *elaborated-type-specifier* refers. This rule also applies to the form of *elaborated-type-specifier* that declares a *class-name* or friend class since it can be construed as referring to the definition of the class. Thus, in any *elaborated-type-specifier*, the `enum` keyword shall be used to refer to an enumeration ([[dcl.enum]](dcl.enum.md)), the `union` *class-key* shall be used to refer to a union ([[class.union]](class.union.md)), and either the `class` or `struct` *class-key* shall be used to refer to a non-union class ([[class.pre]](class.pre.md)). [*Example 1*:

```cpp
enum class E { a, b };
enum E x = E::a;                // OK
struct S { } s;
class S* p = &s;                // OK
```

— *end example*]
