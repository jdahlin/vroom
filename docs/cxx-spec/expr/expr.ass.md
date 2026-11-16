# 7.6.19 Assignment and compound assignment operators [expr.ass]

[Contents](../README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.18 Throwing an exception \[expr.throw\]](expr.throw.md) · next: [7.6.20 Comma operator \[expr.comma\]](expr.comma.md)

**1** The assignment operator (`=`) and the compound assignment operators all group right-to-left. All require a modifiable lvalue as their left operand; their result is an lvalue referring to the left operand. The result in all cases is a bit-field if the left operand is a bit-field. In all cases, the assignment is sequenced after the value computation of the right and left operands, and before the value computation of the assignment expression. The right operand is sequenced before the left operand. With respect to an indeterminately-sequenced function call, the operation of a compound assignment is a single evaluation. [*Note 1*: Therefore, a function call cannot intervene between the lvalue-to-rvalue conversion and the side effect associated with any single compound assignment operator. — *end note*]

```bnf
assignment-expression:
    conditional-expression
    yield-expression
    throw-expression
    logical-or-expression assignment-operator initializer-clause
```

```bnf
assignment-operator: one of
    '= *= /= %= += -= >>= <<= &= ^= |='
```

**2** In simple assignment (`=`), the object referred to by the left operand is modified ([[defns.access]](../intro/intro.defs.md#defns.access)) by replacing its value with the result of the right operand.

**3** If the right operand is an expression, it is implicitly converted ([[conv]](conv.md)) to the cv-unqualified type of the left operand.

**4** When the left operand of an assignment operator is a bit-field that cannot represent the value of the expression, the resulting value of the bit-field is implementation-defined.

**5** A simple assignment whose left operand is of a volatile-qualified type is deprecated ([[depr.volatile.type]](../depr/depr.volatile.type.md)) unless the (possibly parenthesized) assignment is a discarded-value expression or an unevaluated operand.

**6** The behavior of an expression of the form `E1 op= E2` is equivalent to `E1 = E1 op E2` except that `E1` is evaluated only once. Such expressions are deprecated if `E1` has volatile-qualified type; see [[depr.volatile.type]](../depr/depr.volatile.type.md). For `+=` and `-=`, `E1` shall either have arithmetic type or be a pointer to a possibly cv-qualified completely-defined object type. In all other cases, `E1` shall have arithmetic type.

**7** If the value being stored in an object is read via another object that overlaps in any way the storage of the first object, then the overlap shall be exact and the two objects shall have the same type, otherwise the behavior is undefined. [*Note 2*: This restriction applies to the relationship between the left and right sides of the assignment operation; it is not a statement about how the target of the assignment may be aliased in general. See [[basic.lval]](basic.lval.md). — *end note*]

**8** A *braced-init-list* may appear on the right-hand side of

- an assignment to a scalar, in which case the initializer list shall have at most a single element. The meaning of `x = {v}`, where `T` is the scalar type of the expression `x`, is that of `x = T{v}`. The meaning of `x = {}` is `x = T{}`.
- an assignment to an object of class type, in which case the initializer list is passed as the argument to the assignment operator function selected by overload resolution ([[over.ass]](../over/over.ass.md), [[over.match]](../over/over.match.md)).

[*Example 1*:

```cpp
complex<double> z;
z = { 1,2 };        // meaning z.operator=({1,2})
z += { 1, 2 };      // meaning z.operator+=({1,2})
int a, b;
a = b = { 1 };      // meaning a=b=1;
a = { 1 } = b;      // syntax error
```

— *end example*]
