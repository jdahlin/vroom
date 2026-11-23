# 9.2 Specifiers [dcl.spec]

[Contents](../README.md) · up: [9 Declarations \[dcl.dcl\]](dcl.dcl.md) · prev: [9.1 Preamble \[dcl.pre\]](dcl.pre.md) · next: [9.2.1 Storage class specifiers \[dcl.stc\]](dcl.stc.md)

**1** The specifiers that can be used in a declaration are

```bnf
decl-specifier:
    storage-class-specifier
    defining-type-specifier
    function-specifier
    'friend'
    'typedef'
    'constexpr'
    'consteval'
    'constinit'
    'inline'
```

```bnf
decl-specifier-seq:
    decl-specifier attribute-specifier-seqₒₚₜ
    decl-specifier decl-specifier-seq
```

The optional *attribute-specifier-seq* in a *decl-specifier-seq* appertains to the type determined by the preceding *decl-specifier*s ([[dcl.meaning]](dcl.meaning.md)). The *attribute-specifier-seq* affects the type only for the declaration it appears in, not other declarations involving the same type.

**2** Each *decl-specifier* shall appear at most once in a complete *decl-specifier-seq*, except that `long` may appear twice. At most one of the `constexpr`, `consteval`, and `constinit` keywords shall appear in a *decl-specifier-seq*.

**3** If a *type-name* is encountered while parsing a *decl-specifier-seq*, it is interpreted as part of the *decl-specifier-seq* if and only if there is no previous *defining-type-specifier* other than a *cv-qualifier* in the *decl-specifier-seq*. The sequence shall be self-consistent as described below. [*Example 1*:

```cpp
typedef char* Pc;
static Pc;                      // error: name missing
```

Here, the declaration `static` `Pc` is ill-formed because no name was specified for the static variable of type `Pc`. To get a variable called `Pc`, a *type-specifier* (other than `const` or `volatile`) has to be present to indicate that the *typedef-name* `Pc` is the name being (re)declared, rather than being part of the *decl-specifier* sequence. For another example,

```cpp
void f(const Pc);               // void f(char* const) (not const char*)
void g(const int Pc);           // void g(const int)
```

— *end example*]

**4** [*Note 1*: Since `signed`, `unsigned`, `long`, and `short` by default imply `int`, a *type-name* appearing after one of those specifiers is treated as the name being (re)declared. [*Example 2*:

```cpp
void h(unsigned Pc);            // void h(unsigned int)
void k(unsigned int Pc);        // void k(unsigned int)
```

— *end example*] — *end note*]

## Subclauses

- [9.2.1 Storage class specifiers \[dcl.stc\]](dcl.stc.md)
- [9.2.2 Function specifiers \[dcl.fct.spec\]](dcl.fct.spec.md)
- [9.2.3 The `typedef` specifier \[dcl.typedef\]](dcl.typedef.md)
- [9.2.4 The `friend` specifier \[dcl.friend\]](dcl.friend.md)
- [9.2.5 The `constexpr` and `consteval` specifiers \[dcl.constexpr\]](dcl.constexpr.md)
- [9.2.6 The `constinit` specifier \[dcl.constinit\]](dcl.constinit.md)
- [9.2.7 The `inline` specifier \[dcl.inline\]](dcl.inline.md)
- [9.2.8 Type specifiers \[dcl.type\]](dcl.type.md)
  - [9.2.8.1 The *cv-qualifier*s \[dcl.type.cv\]](dcl.type.cv.md)
  - [9.2.8.2 Simple type specifiers \[dcl.type.simple\]](dcl.type.simple.md)
  - [9.2.8.3 Elaborated type specifiers \[dcl.type.elab\]](dcl.type.elab.md)
  - [9.2.8.4 Decltype specifiers \[dcl.type.decltype\]](dcl.type.decltype.md)
  - [9.2.8.5 Placeholder type specifiers \[dcl.spec.auto\]](dcl.spec.auto.md)
    - [9.2.8.5.1 Placeholder type deduction \[dcl.type.auto.deduct\]](dcl.type.auto.deduct.md)
  - [9.2.8.6 Deduced class template specialization types \[dcl.type.class.deduct\]](dcl.type.class.deduct.md)
