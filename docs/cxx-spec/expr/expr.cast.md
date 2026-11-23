# 7.6.3 Explicit type conversion (cast notation) [expr.cast]

[Contents](../README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.2.8 Delete \[expr.delete\]](expr.delete.md) · next: [7.6.4 Pointer-to-member operators \[expr.mptr.oper\]](expr.mptr.oper.md)

**1** The result of the expression `(T)` *cast-expression* is of type `T`. The result is an lvalue if `T` is an lvalue reference type or an rvalue reference to function type and an xvalue if `T` is an rvalue reference to object type; otherwise the result is a prvalue. [*Note 1*: If `T` is a non-class type that is cv-qualified, the *cv-qualifier*s are discarded when determining the type of the resulting prvalue; see [[expr.prop]](expr.prop.md). — *end note*]

**2** An explicit type conversion can be expressed using functional notation ([[expr.type.conv]](expr.type.conv.md)), a type conversion operator (`dynamic_cast`, `static_cast`, `reinterpret_cast`, `const_cast`), or the *cast* notation.

```bnf
cast-expression:
    unary-expression
    '(' type-id ')' cast-expression
```

**3** Any type conversion not mentioned below and not explicitly defined by the user ([[class.conv]](../class/class.conv.md)) is ill-formed.

**4** The conversions performed by

- a `const_cast` ([[expr.const.cast]](expr.const.cast.md)),
- a `static_cast` ([[expr.static.cast]](expr.static.cast.md)),
- a `static_cast` followed by a `const_cast`,
- a `reinterpret_cast` ([[expr.reinterpret.cast]](expr.reinterpret.cast.md)), or
- a `reinterpret_cast` followed by a `const_cast`,

can be performed using the cast notation of explicit type conversion. The same semantic restrictions and behaviors apply, with the exception that in performing a `static_cast` in the following situations the conversion is valid even if the base class is inaccessible:

- a pointer to an object of derived class type or an lvalue or rvalue of derived class type may be explicitly converted to a pointer or reference to an unambiguous base class type, respectively;
- a pointer to member of derived class type may be explicitly converted to a pointer to member of an unambiguous non-virtual base class type;
- a pointer to an object of an unambiguous non-virtual base class type, a glvalue of an unambiguous non-virtual base class type, or a pointer to member of an unambiguous non-virtual base class type may be explicitly converted to a pointer, a reference, or a pointer to member of a derived class type, respectively.

If a conversion can be interpreted in more than one of the ways listed above, the interpretation that appears first in the list is used, even if a cast resulting from that interpretation is ill-formed. If a conversion can be interpreted in more than one way as a `static_cast` followed by a `const_cast`, the conversion is ill-formed. [*Example 1*:

```cpp
struct A { };
struct I1 : A { };
struct I2 : A { };
struct D : I1, I2 { };
A* foo( D* p ) {
  return (A*)( p );             // ill-formed static_cast interpretation
}
```

— *end example*]

**5** The operand of a cast using the cast notation can be a prvalue of type “pointer to incomplete class type”. The destination type of a cast using the cast notation can be “pointer to incomplete class type”. If both the operand and destination types are class types and one or both are incomplete, it is unspecified whether the `static_cast` or the `reinterpret_cast` interpretation is used, even if there is an inheritance relationship between the two classes. [*Note 2*: For example, if the classes were defined later in the translation unit, a multi-pass compiler would be permitted to interpret a cast between pointers to the classes as if the class types were complete at the point of the cast. — *end note*]
