# 7.6.9 Relational operators [expr.rel]

[Contents](../README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.8 Three-way comparison operator \[expr.spaceship\]](expr.spaceship.md) · next: [7.6.10 Equality operators \[expr.eq\]](expr.eq.md)

**1** The relational operators group left-to-right. [*Example 1*: `a<b<c` means `(a<b)<c` and *not* `(a<b)&&(b<c)`. — *end example*]

```bnf
relational-expression:
    compare-expression
    relational-expression '<' compare-expression
    relational-expression '>' compare-expression
    relational-expression '<=' compare-expression
    relational-expression '>=' compare-expression
```

The lvalue-to-rvalue ([[conv.lval]](conv.lval.md)), array-to-pointer ([[conv.array]](conv.array.md)), and function-to-pointer ([[conv.func]](conv.func.md)) standard conversions are performed on the operands. The comparison is deprecated if both operands were of array type prior to these conversions ([[depr.array.comp]](../depr/depr.array.comp.md)).

**2** The converted operands shall have arithmetic, enumeration, or pointer type. The operators `<` (less than), `>` (greater than), `<=` (less than or equal to), and `>=` (greater than or equal to) all yield `false` or `true`. The type of the result is `bool`.

**3** The usual arithmetic conversions ([[expr.arith.conv]](expr.arith.conv.md)) are performed on operands of arithmetic or enumeration type. If both operands are pointers, pointer conversions ([[conv.ptr]](conv.ptr.md)) and qualification conversions ([[conv.qual]](conv.qual.md)) are performed to bring them to their composite pointer type ([[expr.type]](expr.type.md)). After conversions, the operands shall have the same type.

**4** The result of comparing unequal pointers to objects[^1] is defined in terms of a partial order consistent with the following rules:

- If two pointers point to different elements of the same array, or to subobjects thereof, the pointer to the element with the higher subscript is required to compare greater.
- If two pointers point to different non-static data members of the same object, or to subobjects of such members, recursively, the pointer to the later declared member is required to compare greater provided the two members have the same access control ([[class.access]](../class/class.access.md)), neither member is a subobject of zero size, and their class is not a union.
- Otherwise, neither pointer is required to compare greater than the other.

**5** If two operands `p` and `q` compare equal ([[expr.eq]](expr.eq.md)), `p<=q` and `p>=q` both yield `true` and `p<q` and `p>q` both yield `false`. Otherwise, if a pointer `p` compares greater than a pointer `q`, `p>=q`, `p>q`, `q<=p`, and `q<p` all yield `true` and `p<=q`, `p<q`, `q>=p`, and `q>p` all yield `false`. Otherwise, the result of each of the operators is unspecified.

**6** If both operands (after conversions) are of arithmetic or enumeration type, each of the operators shall yield `true` if the specified relationship is true and `false` if it is false.

[^1]: As specified in [[basic.compound]](../basic/basic.compound.md), an object that is not an array element is considered to belong to a single-element array for this purpose and a pointer past the last element of an array of *n* elements is considered to be equivalent to a pointer to a hypothetical array element *n* for this purpose.
