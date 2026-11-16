# 7.6.10 Equality operators [expr.eq]

[Contents](README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.9 Relational operators \[expr.rel\]](expr.rel.md) · next: [7.6.11 Bitwise AND operator \[expr.bit.and\]](expr.bit.and.md)

```bnf
equality-expression:
    relational-expression
    equality-expression '==' relational-expression
    equality-expression '!=' relational-expression
```

**1** The `==` (equal to) and the `!=` (not equal to) operators group left-to-right. The lvalue-to-rvalue ([[conv.lval]](conv.lval.md)), array-to-pointer ([[conv.array]](conv.array.md)), and function-to-pointer ([[conv.func]](conv.func.md)) standard conversions are performed on the operands. The comparison is deprecated if both operands were of array type prior to these conversions ([[depr.array.comp]](depr.array.comp.md)).

**2** The converted operands shall have arithmetic, enumeration, pointer, or pointer-to-member type, or type `std::nullptr_t`. The operators `==` and `!=` both yield `true` or `false`, i.e., a result of type `bool`. In each case below, the operands shall have the same type after the specified conversions have been applied.

**3** If at least one of the operands is a pointer, pointer conversions ([[conv.ptr]](conv.ptr.md)), function pointer conversions ([[conv.fctptr]](conv.fctptr.md)), and qualification conversions ([[conv.qual]](conv.qual.md)) are performed on both operands to bring them to their composite pointer type ([[expr.type]](expr.type.md)). Comparing pointers is defined as follows:

- If one pointer represents the address of a complete object, and another pointer represents the address one past the last element of a different complete object,[^1] the result of the comparison is unspecified.
- Otherwise, if the pointers are both null, both point to the same function, or both represent the same address ([[basic.compound]](basic.compound.md)), they compare equal.
- Otherwise, the pointers compare unequal.

**4** If at least one of the operands is a pointer to member, pointer-to-member conversions ([[conv.mem]](conv.mem.md)), function pointer conversions ([[conv.fctptr]](conv.fctptr.md)), and qualification conversions ([[conv.qual]](conv.qual.md)) are performed on both operands to bring them to their composite pointer type ([[expr.type]](expr.type.md)). Comparing pointers to members is defined as follows:

- If two pointers to members are both the null member pointer value, they compare equal.

- If only one of two pointers to members is the null member pointer value, they compare unequal.

- If either is a pointer to a virtual member function, the result is unspecified.

- If one refers to a member of class `C1` and the other refers to a member of a different class `C2`, where neither is a base class of the other, the result is unspecified. [*Example 1*:

  ```cpp
  struct A {};
  struct B : A { int x; };
  struct C : A { int x; };

  int A::*bx = (int(A::*))&B::x;
  int A::*cx = (int(A::*))&C::x;

  bool b1 = (bx == cx);   // unspecified
  ```

  — *end example*]

- If both refer to (possibly different) members of the same union ([[class.union]](class.union.md)), they compare equal.

- Otherwise, two pointers to members compare equal if they would refer to the same member of the same most derived object ([[intro.object]](intro.object.md)) or the same subobject if indirection with a hypothetical object of the associated class type were performed, otherwise they compare unequal. [*Example 2*:

  ```cpp
  struct B {
    int f();
  };
  struct L : B { };
  struct R : B { };
  struct D : L, R { };

  int (B::*pb)() = &B::f;
  int (L::*pl)() = pb;
  int (R::*pr)() = pb;
  int (D::*pdl)() = pl;
  int (D::*pdr)() = pr;
  bool x = (pdl == pdr);          // false
  bool y = (pb == pl);            // true
  ```

  — *end example*]

**5** Two operands of type `std::nullptr_t` or one operand of type `std::nullptr_t` and the other a null pointer constant compare equal.

**6** If two operands compare equal, the result is `true` for the `==` operator and `false` for the `!=` operator. If two operands compare unequal, the result is `false` for the `==` operator and `true` for the `!=` operator. Otherwise, the result of each of the operators is unspecified.

**7** If both operands are of arithmetic or enumeration type, the usual arithmetic conversions ([[expr.arith.conv]](expr.arith.conv.md)) are performed on both operands; each of the operators shall yield `true` if the specified relationship is true and `false` if it is false.

[^1]: As specified in [[basic.compound]](basic.compound.md), an object that is not an array element is considered to belong to a single-element array for this purpose.
