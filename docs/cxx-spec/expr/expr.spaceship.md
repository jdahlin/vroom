# 7.6.8 Three-way comparison operator [expr.spaceship]

[Contents](../README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.7 Shift operators \[expr.shift\]](expr.shift.md) · next: [7.6.9 Relational operators \[expr.rel\]](expr.rel.md)

**1** The three-way comparison operator groups left-to-right.

```bnf
compare-expression:
    shift-expression
    compare-expression '<=>' shift-expression
```

**2** The expression `p <=> q` is a prvalue indicating whether `p` is less than, equal to, greater than, or incomparable with `q`.

**3** If one of the operands is of type `bool` and the other is not, the program is ill-formed.

**4** If both operands have arithmetic types, or one operand has integral type and the other operand has unscoped enumeration type, the usual arithmetic conversions ([[expr.arith.conv]](expr.arith.conv.md)) are applied to the operands. Then:

- If a narrowing conversion ([[dcl.init.list]](../dcl/dcl.init.list.md)) is required, other than from an integral type to a floating-point type, the program is ill-formed.
- Otherwise, if the operands have integral type, the result is of type `std::strong_ordering`. The result is `std::strong_ordering::equal` if both operands are arithmetically equal, `std::strong_ordering::less` if the first operand is arithmetically less than the second operand, and `std::strong_ordering::greater` otherwise.
- Otherwise, the operands have floating-point type, and the result is of type `std::partial_ordering`. The expression `a <=> b` yields `std::partial_ordering::less` if `a` is less than `b`, `std::partial_ordering::greater` if `a` is greater than `b`, `std::partial_ordering::equivalent` if `a` is equivalent to `b`, and `std::partial_ordering::unordered` otherwise.

**5** If both operands have the same enumeration type `E`, the operator yields the result of converting the operands to the underlying type of `E` and applying `<=>` to the converted operands.

**6** If at least one of the operands is of pointer type and the other operand is of pointer or array type, array-to-pointer conversions ([[conv.array]](conv.array.md)), pointer conversions ([[conv.ptr]](conv.ptr.md)), and qualification conversions ([[conv.qual]](conv.qual.md)) are performed on both operands to bring them to their composite pointer type ([[expr.type]](expr.type.md)). After the conversions, the operands shall have the same type. [*Note 1*: If both of the operands are arrays, array-to-pointer conversions ([[conv.array]](conv.array.md)) are not applied. — *end note*]

**7** If the composite pointer type is an object pointer type, `p <=> q` is of type `std::strong_ordering`. If two pointer operands `p` and `q` compare equal ([[expr.eq]](expr.eq.md)), `p <=> q` yields `std::strong_ordering::equal`; if `p` and `q` compare unequal, `p <=> q` yields `std::strong_ordering::less` if `q` compares greater than `p` and `std::strong_ordering::greater` if `p` compares greater than `q` ([[expr.rel]](expr.rel.md)). Otherwise, the result is unspecified.

**8** Otherwise, the program is ill-formed.

**9** The three comparison category types ([[cmp.categories]](../lib/support/cmp.categories.md)) (the types `std::strong_ordering`, `std::weak_ordering`, and `std::partial_ordering`) are not predefined; if the header `<compare>` ([[compare.syn]](../lib/support/compare.syn.md)) is not imported or included prior to a use of such a class type – even an implicit use in which the type is not named (e.g., via the `auto` specifier ([[dcl.spec.auto]](../dcl/dcl.spec.auto.md)) in a defaulted three-way comparison ([[class.spaceship]](../class/class.spaceship.md)) or use of the built-in operator) – the program is ill-formed.
