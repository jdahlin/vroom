# 9.2.7 The `inline` specifier [dcl.inline]

[Contents](README.md) · up: [9.2 Specifiers \[dcl.spec\]](dcl.spec.md) · prev: [9.2.6 The `constinit` specifier \[dcl.constinit\]](dcl.constinit.md) · next: [9.2.8 Type specifiers \[dcl.type\]](dcl.type.md)

**1** The `inline` specifier shall be applied only to the declaration of a variable or function.

**2** A function declaration ([[dcl.fct]](dcl.fct.md), [[class.mfct]](class.mfct.md), [[class.friend]](class.friend.md)) with an `inline` specifier declares an *inline function*. The inline specifier indicates to the implementation that inline substitution of the function body at the point of call is to be preferred to the usual function call mechanism. An implementation is not required to perform this inline substitution at the point of call; however, even if this inline substitution is omitted, the other rules for inline functions specified in this subclause shall still be respected. [*Note 1*: The `inline` keyword has no effect on the linkage of a function. In certain cases, an inline function cannot use names with internal linkage; see [[basic.link]](basic.link.md). — *end note*]

**3** A variable declaration with an `inline` specifier declares an *inline variable*.

**4** The `inline` specifier shall not appear on a block scope declaration or on the declaration of a function parameter. If the `inline` specifier is used in a friend function declaration, that declaration shall be a definition or the function shall have previously been declared inline.

**5** If a definition of a function or variable is reachable at the point of its first declaration as inline, the program is ill-formed. If a function or variable with external or module linkage is declared inline in one definition domain, an inline declaration of it shall be reachable from the end of every definition domain in which it is declared; no diagnostic is required. [*Note 2*: A call to an inline function or a use of an inline variable may be encountered before its definition becomes reachable in a translation unit. — *end note*]

**6** [*Note 3*: An inline function or variable with external or module linkage has the same address in all translation units. A `static` local variable in an inline function with external or module linkage always refers to the same object. A type defined within the body of an inline function with external or module linkage is the same type in every translation unit. — *end note*]

**7** If an inline function or variable that is attached to a named module is declared in a definition domain, it shall be defined in that domain. [*Note 4*: A constexpr function ([[dcl.constexpr]](dcl.constexpr.md)) is implicitly inline. In the global module, a function defined within a class definition is implicitly inline ([[class.mfct]](class.mfct.md), [[class.friend]](class.friend.md)). — *end note*]
