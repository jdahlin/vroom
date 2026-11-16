# 7.3.6 Integral promotions [conv.prom]

[Contents](README.md) · up: [7.3 Standard conversions \[conv\]](conv.md) · prev: [7.3.5 Qualification conversions \[conv.qual\]](conv.qual.md) · next: [7.3.7 Floating-point promotion \[conv.fpprom\]](conv.fpprom.md)

**1** A prvalue of an integer type other than `bool`, `char16_t`, `char32_t`, or `wchar_t` whose integer conversion rank ([[conv.rank]](conv.rank.md)) is less than the rank of `int` can be converted to a prvalue of type `int` if `int` can represent all the values of the source type; otherwise, the source prvalue can be converted to a prvalue of type `unsigned int`.

**2** A prvalue of type `char16_t`, `char32_t`, or `wchar_t` ([[basic.fundamental]](basic.fundamental.md)) can be converted to a prvalue of the first of the following types that can represent all the values of its underlying type: `int`, `unsigned int`, `long int`, `unsigned long int`, `long long int`, or `unsigned long long int`. If none of the types in that list can represent all the values of its underlying type, a prvalue of type `char16_t`, `char32_t`, or `wchar_t` can be converted to a prvalue of its underlying type.

**3** A prvalue of an unscoped enumeration type whose underlying type is not fixed can be converted to a prvalue of the first of the following types that can represent all the values of the enumeration ([[dcl.enum]](dcl.enum.md)): `int`, `unsigned int`, `long int`, `unsigned long int`, `long long int`, or `unsigned long long int`. If none of the types in that list can represent all the values of the enumeration, a prvalue of an unscoped enumeration type can be converted to a prvalue of the extended integer type with lowest integer conversion rank ([[conv.rank]](conv.rank.md)) greater than the rank of `long long` in which all the values of the enumeration can be represented. If there are two such extended types, the signed one is chosen.

**4** A prvalue of an unscoped enumeration type whose underlying type is fixed ([[dcl.enum]](dcl.enum.md)) can be converted to a prvalue of its underlying type. Moreover, if integral promotion can be applied to its underlying type, a prvalue of an unscoped enumeration type whose underlying type is fixed can also be converted to a prvalue of the promoted underlying type.

**5** A prvalue for an integral bit-field ([[class.bit]](class.bit.md)) can be converted to a prvalue of type `int` if `int` can represent all the values of the bit-field; otherwise, it can be converted to `unsigned int` if `unsigned int` can represent all the values of the bit-field. If the bit-field is larger yet, no integral promotion applies to it. If the bit-field has an enumerated type, it is treated as any other value of that type for promotion purposes.

**6** A prvalue of type `bool` can be converted to a prvalue of type `int`, with `false` becoming zero and `true` becoming one.

**7** These conversions are called *integral promotions*.
