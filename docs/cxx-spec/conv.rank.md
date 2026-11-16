# 6.8.4 Integer conversion rank [conv.rank]

[Contents](README.md) · up: [6.8 Types \[basic.types\]](basic.types.md) · prev: [6.8.3 CV-qualifiers \[basic.type.qualifier\]](basic.type.qualifier.md) · next: [6.9 Program execution \[basic.exec\]](basic.exec.md)

**1** Every integer type has an *integer conversion rank* defined as follows:

- No two signed integer types other than `char` and `signed char` (if `char` is signed) shall have the same rank, even if they have the same representation.
- The rank of a signed integer type shall be greater than the rank of any signed integer type with a smaller width.
- The rank of `long long int` shall be greater than the rank of `long int`, which shall be greater than the rank of `int`, which shall be greater than the rank of `short int`, which shall be greater than the rank of `signed char`.
- The rank of any unsigned integer type shall equal the rank of the corresponding signed integer type.
- The rank of any standard integer type shall be greater than the rank of any extended integer type with the same width.
- The rank of `char` shall equal the rank of `signed char` and `unsigned char`.
- The rank of `bool` shall be less than the rank of all other standard integer types.
- The ranks of `char8_t`, `char16_t`, `char32_t`, and `wchar_t` shall equal the ranks of their underlying types ([[basic.fundamental]](basic.fundamental.md)).
- The rank of any extended signed integer type relative to another extended signed integer type with the same width is implementation-defined, but still subject to the other rules for determining the integer conversion rank.
- For all integer types `T1`, `T2`, and `T3`, if `T1` has greater rank than `T2` and `T2` has greater rank than `T3`, then `T1` shall have greater rank than `T3`.

[*Note 1*: The integer conversion rank is used in the definition of the integral promotions ([[conv.prom]](conv.prom.md)) and the usual arithmetic conversions ([[expr.arith.conv]](expr.arith.conv.md)). — *end note*]
