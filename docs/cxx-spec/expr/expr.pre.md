# 7.1 Preamble [expr.pre]

[Contents](../README.md) · up: [7 Expressions \[expr\]](expr.md) · prev: [7 Expressions \[expr\]](expr.md) · next: [7.2 Properties of expressions \[expr.prop\]](expr.prop.md)

**1** [*Note 1*: [[expr]](expr.md) defines the syntax, order of evaluation, and meaning of expressions.[^1] An expression is a sequence of operators and operands that specifies a computation. An expression can result in a value and can cause side effects. — *end note*]

**2** [*Note 2*: Operators can be overloaded, that is, given meaning when applied to expressions of class type ([[class]](../class/class.md)) or enumeration type ([[dcl.enum]](../dcl/dcl.enum.md)). Uses of overloaded operators are transformed into function calls as described in [[over.oper]](../over/over.oper.md). Overloaded operators obey the rules for syntax and evaluation order specified in [[expr.compound]](expr.compound.md), but the requirements of operand type and value category are replaced by the rules for function call. Relations between operators, such as `++a` meaning `a+=1`, are not guaranteed for overloaded operators ([[over.oper]](../over/over.oper.md)). — *end note*]

**3** Subclause [[expr.compound]](expr.compound.md) defines the effects of operators when applied to types for which they have not been overloaded. Operator overloading shall not modify the rules for the *built-in operators*, that is, for operators applied to types for which they are defined by this Standard. However, these built-in operators participate in overload resolution, and as part of that process user-defined conversions will be considered where necessary to convert the operands to types appropriate for the built-in operator. If a built-in operator is selected, such conversions will be applied to the operands before the operation is considered further according to the rules in subclause [[expr.compound]](expr.compound.md); see [[over.match.oper]](../over/over.match.oper.md), [[over.built]](../over/over.built.md).

**4** If during the evaluation of an expression, the result is not mathematically defined or not in the range of representable values for its type, the behavior is undefined. [*Note 3*: Treatment of division by zero, forming a remainder using a zero divisor, and all floating-point exceptions varies among machines, and is sometimes adjustable by a library function. — *end note*]

**5** [*Note 4*: The implementation may regroup operators according to the usual mathematical rules only where the operators really are associative or commutative.[^2] For example, in the following fragment

```cpp
int a, b;
/* ... */
a = a + 32760 + b + 5;
```

the expression statement behaves exactly the same as

```cpp
a = (((a + 32760) + b) + 5);
```

due to the associativity and precedence of these operators. Thus, the result of the sum `(a + 32760)` is next added to `b`, and that result is then added to 5 which results in the value assigned to `a`. On a machine in which overflows produce an exception and in which the range of values representable by an `int` is `[-32768, +32767]`, the implementation cannot rewrite this expression as

```cpp
a = ((a + b) + 32765);
```

since if the values for `a` and `b` were, respectively, -32754 and -15, the sum `a + b` would produce an exception while the original expression would not; nor can the expression be rewritten as either

```cpp
a = ((a + 32765) + b);
```

or

```cpp
a = (a + (b + 32765));
```

since the values for `a` and `b` might have been, respectively, 4 and -8 or -17 and 12. However on a machine in which overflows do not produce an exception and in which the results of overflows are reversible, the above expression statement can be rewritten by the implementation in any of the above ways because the same result will occur. — *end note*]

**6** The values of the floating-point operands and the results of floating-point expressions may be represented in greater precision and range than that required by the type; the types are not changed thereby.[^3]

[^1]: The precedence of operators is not directly specified, but it can be derived from the syntax.
[^2]: Overloaded operators are never assumed to be associative or commutative.
[^3]: The cast and assignment operators must still perform their specific conversions as described in [[expr.type.conv]](expr.type.conv.md), [[expr.cast]](expr.cast.md), [[expr.static.cast]](expr.static.cast.md) and [[expr.ass]](expr.ass.md).
