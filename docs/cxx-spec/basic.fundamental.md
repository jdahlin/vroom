# 6.8.1 Fundamental types [basic.fundamental]

[Contents](README.md) · up: [6.8 Types \[basic.types\]](basic.types.md) · prev: [6.8 Types \[basic.types\]](basic.types.md) · next: [6.8.2 Compound types \[basic.compound\]](basic.compound.md)

**1** There are five *standard signed integer types*: “`signed char`”, “`short int`”, “`int`”, “`long int`”, and “`long long int`”. In this list, each type provides at least as much storage as those preceding it in the list. There may also be implementation-defined *extended signed integer types*. The standard and extended signed integer types are collectively called *signed integer types*. The range of representable values for a signed integer type is -2<sup>*N*-1</sup> to 2<sup>*N*-1</sup>-1 (inclusive), where *N* is called the *width* of the type. [*Note 1*: Plain `int`s are intended to have the natural width suggested by the architecture of the execution environment; the other signed integer types are provided to meet special needs. — *end note*]

**2** For each of the standard signed integer types, there exists a corresponding (but different) *standard unsigned integer type*: “`unsigned char`”, “`unsigned short int`”, “`unsigned int`”, “`unsigned long int`”, and “`unsigned long long int`”. Likewise, for each of the extended signed integer types, there exists a corresponding *extended unsigned integer type*. The standard and extended unsigned integer types are collectively called *unsigned integer types*. An unsigned integer type has the same width *N* as the corresponding signed integer type. The range of representable values for the unsigned type is 0 to 2<sup>*N*</sup>-1 (inclusive); arithmetic for the unsigned type is performed modulo 2<sup>*N*</sup>. [*Note 2*: Unsigned arithmetic does not overflow. Overflow for signed arithmetic yields undefined behavior ([[expr.pre]](expr.pre.md)). — *end note*]

**3** An unsigned integer type has the same object representation, value representation, and alignment requirements ([[basic.align]](basic.align.md)) as the corresponding signed integer type. For each value *x* of a signed integer type, the value of the corresponding unsigned integer type congruent to *x* modulo 2<sup>*N*</sup> has the same value of corresponding bits in its value representation.[^1] [*Example 1*: The value -1 of a signed integer type has the same representation as the largest value of the corresponding unsigned type. — *end example*]

<a id="tab:basic.fundamental.width"></a>
**Table 12: Minimum width** [tab:basic.fundamental.width]

| Type | Minimum width *N* |
| --- | --- |
| `signed char` | 8 |
| `short` | 16 |
| `int` | 16 |
| `long` | 32 |
| `long long` | 64 |

**4** The width of each signed integer type shall not be less than the values specified in [Table 12](basic.fundamental.md#tab:basic.fundamental.width). The value representation of a signed or unsigned integer type comprises *N* bits, where N is the respective width. Each set of values for any padding bits ([[basic.types]](basic.types.md)) in the object representation are alternative representations of the value specified by the value representation. [*Note 3*: Padding bits have unspecified value, but cannot cause traps. In contrast, see ISO C 6.2.6.2. — *end note*] [*Note 4*: The signed and unsigned integer types satisfy the constraints given in ISO C 5.2.4.2.1. — *end note*] Except as specified above, the width of a signed or unsigned integer type is implementation-defined.

**5** Each value *x* of an unsigned integer type with width *N* has a unique representation *x* = *x*<sub>0</sub> 2<sup>0</sup> + *x*<sub>1</sub> 2<sup>1</sup> + …+ *x*<sub>*N*-1</sub> 2<sup>*N*-1</sup>, where each coefficient *x*<sub>*i*</sub> is either 0 or 1; this is called the *base-2 representation* of *x*. The base-2 representation of a value of signed integer type is the base-2 representation of the congruent value of the corresponding unsigned integer type. The standard signed integer types and standard unsigned integer types are collectively called the *standard integer types*, and the extended signed integer types and extended unsigned integer types are collectively called the *extended integer types*.

**6** A fundamental type specified to have a signed or unsigned integer type as its *underlying type* has the same object representation, value representation, alignment requirements ([[basic.align]](basic.align.md)), and range of representable values as the underlying type. Further, each value has the same representation in both types.

**7** Type `char` is a distinct type that has an implementation-defined choice of “`signed char`” or “`unsigned char`” as its underlying type. The values of type `char` can represent distinct codes for all members of the implementation’s basic character set. The three types `char`, `signed char`, and `unsigned char` are collectively called *ordinary character types*. The ordinary character types and `char8_t` are collectively called *narrow character types*. For narrow character types, each possible bit pattern of the object representation represents a distinct value. [*Note 5*: This requirement does not hold for other types. — *end note*] [*Note 6*: A bit-field of narrow character type whose width is larger than the width of that type has padding bits; see [[basic.types]](basic.types.md). — *end note*]

**8** Type `wchar_t` is a distinct type that has an implementation-defined signed or unsigned integer type as its underlying type. The values of type `wchar_t` can represent distinct codes for all members of the largest extended character set specified among the supported locales ([[locale]](lib/locale.md)).

**9** Type `char8_t` denotes a distinct type whose underlying type is `unsigned char`. Types `char16_t` and `char32_t` denote distinct types whose underlying types are `uint_least16_t` and `uint_least32_t`, respectively, in `<cstdint>` ([[cstdint.syn]](lib/cstdint.syn.md)).

**10** Type `bool` is a distinct type that has the same object representation, value representation, and alignment requirements as an implementation-defined unsigned integer type. The values of type `bool` are `true` and `false`. [*Note 7*: There are no `signed`, `unsigned`, `short`, or `long bool` types or values. — *end note*]

**11** Types `bool`, `char`, `wchar_t`, `char8_t`, `char16_t`, `char32_t`, and the signed and unsigned integer types are collectively called *integral types*. A synonym for integral type is *integer type*. [*Note 8*: Enumerations ([[dcl.enum]](dcl.enum.md)) are not integral; however, unscoped enumerations can be promoted to integral types as specified in [[conv.prom]](conv.prom.md). — *end note*]

**12** There are three *floating-point types*: `float`, `double`, and `long double`. The type `double` provides at least as much precision as `float`, and the type `long double` provides at least as much precision as `double`. The set of values of the type `float` is a subset of the set of values of the type `double`; the set of values of the type `double` is a subset of the set of values of the type `long double`. The value representation of floating-point types is implementation-defined. [*Note 9*: This document imposes no requirements on the accuracy of floating-point operations; see also [[support.limits]](lib/support.limits.md). — *end note*] Integral and floating-point types are collectively called *arithmetic* types. Specializations of the standard library template `std::numeric_limits` ([[support.limits]](lib/support.limits.md)) shall specify the maximum and minimum values of each arithmetic type for an implementation.

**13** A type *cv* `void` is an incomplete type that cannot be completed; such a type has an empty set of values. It is used as the return type for functions that do not return a value. Any expression can be explicitly converted to type *cv* `void` ([[expr.type.conv]](expr.type.conv.md), [[expr.static.cast]](expr.static.cast.md), [[expr.cast]](expr.cast.md)). An expression of type *cv* `void` shall be used only as an expression statement ([[stmt.expr]](stmt.expr.md)), as an operand of a comma expression ([[expr.comma]](expr.comma.md)), as a second or third operand of `?:` ([[expr.cond]](expr.cond.md)), as the operand of `typeid`, `noexcept`, or `decltype`, as the expression in a `return` statement ([[stmt.return]](stmt.return.md)) for a function with the return type *cv* `void`, or as the operand of an explicit conversion to type *cv* `void`.

**14** A value of type `std::nullptr_t` is a null pointer constant ([[conv.ptr]](conv.ptr.md)). Such values participate in the pointer and the pointer-to-member conversions ([[conv.ptr]](conv.ptr.md), [[conv.mem]](conv.mem.md)). `sizeof(std::nullptr_t)` shall be equal to `sizeof(void*)`.

**15** The types described in this subclause are called *fundamental types*. [*Note 10*: Even if the implementation defines two or more fundamental types to have the same value representation, they are nevertheless different types. — *end note*]

[^1]: This is also known as two’s complement representation.
