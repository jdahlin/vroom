# 7.6.4 Pointer-to-member operators [expr.mptr.oper]

[Contents](../README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.3 Explicit type conversion (cast notation) \[expr.cast\]](expr.cast.md) · next: [7.6.5 Multiplicative operators \[expr.mul\]](expr.mul.md)

**1** The pointer-to-member operators `->*` and `.*` group left-to-right.

```bnf
pm-expression:
    cast-expression
    pm-expression '.*' cast-expression
    pm-expression '->*' cast-expression
```

**2** The binary operator `.*` binds its second operand, which shall be of type “pointer to member of `T`” to its first operand, which shall be a glvalue of class `T` or of a class of which `T` is an unambiguous and accessible base class. The result is an object or a function of the type specified by the second operand.

**3** The binary operator `->*` binds its second operand, which shall be of type “pointer to member of `T`” to its first operand, which shall be of type “pointer to `U`” where `U` is either `T` or a class of which `T` is an unambiguous and accessible base class. The expression `E1->*E2` is converted into the equivalent form `(*(E1)).*E2`.

**4** Abbreviating *pm-expression*`.*`*cast-expression* as `E1.*E2`, `E1` is called the *object expression*. If the dynamic type of `E1` does not contain the member to which `E2` refers, the behavior is undefined. Otherwise, the expression `E1` is sequenced before the expression `E2`.

**5** The restrictions on cv-qualification, and the manner in which the cv-qualifiers of the operands are combined to produce the cv-qualifiers of the result, are the same as the rules for `E1.E2` given in [[expr.ref]](expr.ref.md). [*Note 1*: It is not possible to use a pointer to member that refers to a `mutable` member to modify a const class object. For example,

```cpp
struct S {
  S() : i(0) { }
  mutable int i;
};
void f()
{
const S cs;
int S::* pm = &S::i;            // pm refers to mutable member S::i
cs.*pm = 88;                    // error: cs is a const object
}
```

— *end note*]

**6** If the result of `.*` or `->*` is a function, then that result can be used only as the operand for the function call operator `()`. [*Example 1*:

```cpp
(ptr_to_obj->*ptr_to_mfct)(10);
```

calls the member function denoted by `ptr_to_mfct` for the object pointed to by `ptr_to_obj`. — *end example*] In a `.*` expression whose object expression is an rvalue, the program is ill-formed if the second operand is a pointer to member function whose *ref-qualifier* is `&`, unless its *cv-qualifier-seq* is `const`. In a `.*` expression whose object expression is an lvalue, the program is ill-formed if the second operand is a pointer to member function whose *ref-qualifier* is `&&`. The result of a `.*` expression whose second operand is a pointer to a data member is an lvalue if the first operand is an lvalue and an xvalue otherwise. The result of a `.*` expression whose second operand is a pointer to a member function is a prvalue. If the second operand is the null member pointer value ([[conv.mem]](conv.mem.md)), the behavior is undefined.
