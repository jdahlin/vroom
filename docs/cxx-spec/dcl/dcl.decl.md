# 9.3 Declarators [dcl.decl]

[Contents](../README.md) · up: [9 Declarations \[dcl.dcl\]](dcl.dcl.md) · prev: [9.2.8.6 Deduced class template specialization types \[dcl.type.class.deduct\]](dcl.type.class.deduct.md) · next: [9.3.1 Type names \[dcl.name\]](dcl.name.md)

**1** A declarator declares a single variable, function, or type, within a declaration. The *init-declarator-list* appearing in a declaration is a comma-separated sequence of declarators, each of which can have an initializer.

```bnf
init-declarator-list:
    init-declarator
    init-declarator-list ',' init-declarator
```

```bnf
init-declarator:
    declarator initializerₒₚₜ
    declarator requires-clause
```

**2** The three components of a *simple-declaration* are the attributes ([[dcl.attr]](dcl.attr.md)), the specifiers (*decl-specifier-seq*; [[dcl.spec]](dcl.spec.md)) and the declarators (*init-declarator-list*). The specifiers indicate the type, storage class or other properties of the entities being declared. The declarators specify the names of these entities and (optionally) modify the type of the specifiers with operators such as `*` (pointer to) and `()` (function returning). Initial values can also be specified in a declarator; initializers are discussed in [[dcl.init]](dcl.init.md) and [[class.init]](../class/class.init.md).

**3** Each *init-declarator* in a declaration is analyzed separately as if it was in a declaration by itself. [*Note 1*: A declaration with several declarators is usually equivalent to the corresponding sequence of declarations each with a single declarator. That is

```cpp
T D1, D2, ... Dn;
```

is usually equivalent to

```cpp
T D1; T D2; ... T Dn;
```

where `T` is a *decl-specifier-seq* and each `Di` is an *init-declarator*. One exception is when a name introduced by one of the *declarator*s hides a type name used by the *decl-specifier*s, so that when the same *decl-specifier*s are used in a subsequent declaration, they do not have the same meaning, as in

```cpp
struct S { /* ... */ };
S S, T;                 // declare two instances of struct S
```

which is not equivalent to

```cpp
struct S { /* ... */ };
S S;
S T;                    // error
```

Another exception is when `T` is `auto` ([[dcl.spec.auto]](dcl.spec.auto.md)), for example:

```cpp
auto i = 1, j = 2.0;    // error: deduced types for i and j do not match
```

as opposed to

```cpp
auto i = 1;             // OK: i deduced to have type int
auto j = 2.0;           // OK: j deduced to have type double
```

— *end note*]

**4** The optional *requires-clause* ([[temp.pre]](../temp/temp.pre.md)) in an *init-declarator* or *member-declarator* shall be present only if the declarator declares a templated function ([[dcl.fct]](dcl.fct.md)). When present after a declarator, the *requires-clause* is called the *trailing *requires-clause**. The trailing *requires-clause* introduces the *constraint-expression* that results from interpreting its *constraint-logical-or-expression* as a *constraint-expression*. [*Example 1*:

```cpp
void f1(int a) requires true;               // error: non-templated function
template<typename T>
  auto f2(T a) -> bool requires true;       // OK
template<typename T>
  auto f3(T a) requires true -> bool;       // error: requires-clause precedes trailing-return-type
void (*pf)() requires true;                 // error: constraint on a variable
void g(int (*)() requires true);            // error: constraint on a parameter-declaration

auto* p = new void(*)(char) requires true;  // error: not a function declaration
```

— *end example*]

**5** Declarators have the syntax

```bnf
declarator:
    ptr-declarator
    noptr-declarator parameters-and-qualifiers trailing-return-type
```

```bnf
ptr-declarator:
    noptr-declarator
    ptr-operator ptr-declarator
```

```bnf
noptr-declarator:
    declarator-id attribute-specifier-seqₒₚₜ
    noptr-declarator parameters-and-qualifiers
    noptr-declarator '[' constant-expressionₒₚₜ ']' attribute-specifier-seqₒₚₜ
    '(' ptr-declarator ')'
```

```bnf
parameters-and-qualifiers:
    '(' parameter-declaration-clause ')' cv-qualifier-seqₒₚₜ
        ref-qualifierₒₚₜ noexcept-specifierₒₚₜ attribute-specifier-seqₒₚₜ
```

```bnf
trailing-return-type:
    '->' type-id
```

```bnf
ptr-operator:
    '*' attribute-specifier-seqₒₚₜ cv-qualifier-seqₒₚₜ
    '&' attribute-specifier-seqₒₚₜ
    '&&' attribute-specifier-seqₒₚₜ
    nested-name-specifier '*' attribute-specifier-seqₒₚₜ cv-qualifier-seqₒₚₜ
```

```bnf
cv-qualifier-seq:
    cv-qualifier cv-qualifier-seqₒₚₜ
```

```bnf
cv-qualifier:
    'const'
    'volatile'
```

```bnf
ref-qualifier:
    '&'
    '&&'
```

```bnf
declarator-id:
    '...'ₒₚₜ id-expression
```

## Subclauses

- [9.3.1 Type names \[dcl.name\]](dcl.name.md)
- [9.3.2 Ambiguity resolution \[dcl.ambig.res\]](dcl.ambig.res.md)
- [9.3.3 Meaning of declarators \[dcl.meaning\]](dcl.meaning.md)
  - [9.3.3.1 Pointers \[dcl.ptr\]](dcl.ptr.md)
  - [9.3.3.2 References \[dcl.ref\]](dcl.ref.md)
  - [9.3.3.3 Pointers to members \[dcl.mptr\]](dcl.mptr.md)
  - [9.3.3.4 Arrays \[dcl.array\]](dcl.array.md)
  - [9.3.3.5 Functions \[dcl.fct\]](dcl.fct.md)
  - [9.3.3.6 Default arguments \[dcl.fct.default\]](dcl.fct.default.md)
