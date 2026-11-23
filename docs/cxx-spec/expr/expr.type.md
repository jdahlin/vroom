# 7.2.2 Type [expr.type]

[Contents](../README.md) · up: [7.2 Properties of expressions \[expr.prop\]](expr.prop.md) · prev: [7.2.1 Value category \[basic.lval\]](basic.lval.md) · next: [7.2.3 Context dependence \[expr.context\]](expr.context.md)

**1** If an expression initially has the type “reference to `T`” ([[dcl.ref]](../dcl/dcl.ref.md), [[dcl.init.ref]](../dcl/dcl.init.ref.md)), the type is adjusted to `T` prior to any further analysis. The expression designates the object or function denoted by the reference, and the expression is an lvalue or an xvalue, depending on the expression. [*Note 1*: Before the lifetime of the reference has started or after it has ended, the behavior is undefined (see [[basic.life]](../basic/basic.life.md)). — *end note*]

**2** If a prvalue initially has the type “*cv* `T`”, where `T` is a cv-unqualified non-class, non-array type, the type of the expression is adjusted to `T` prior to any further analysis.

**3** The *composite pointer type* of two operands `p1` and `p2` having types `T1` and `T2`, respectively, where at least one is a pointer or pointer-to-member type or `std::nullptr_t`, is:

- if both `p1` and `p2` are null pointer constants, `std::nullptr_t`;
- if either `p1` or `p2` is a null pointer constant, `T2` or `T1`, respectively;
- if `T1` or `T2` is “pointer to *cv1* `void`” and the other type is “pointer to *cv2* `T`”, where `T` is an object type or `void`, “pointer to *cv12* `void`”, where *cv12* is the union of *cv1* and *cv2*;
- if `T1` or `T2` is “pointer to `noexcept` function” and the other type is “pointer to function”, where the function types are otherwise the same, “pointer to function”;
- if `T1` is “pointer to *cv1* `C1`” and `T2` is “pointer to *cv2* `C2`”, where `C1` is reference-related to `C2` or `C2` is reference-related to `C1` ([[dcl.init.ref]](../dcl/dcl.init.ref.md)), the cv-combined type ([[conv.qual]](conv.qual.md)) of `T1` and `T2` or the cv-combined type of `T2` and `T1`, respectively;
- if `T1` or `T2` is “pointer to member of `C1` of type function”, the other type is “pointer to member of `C2` of type `noexcept` function”, and `C1` is reference-related to `C2` or `C2` is reference-related to `C1` ([[dcl.init.ref]](../dcl/dcl.init.ref.md)), where the function types are otherwise the same, “pointer to member of `C2` of type function” or “pointer to member of `C1` of type function”, respectively;
- if `T1` is “pointer to member of `C1` of type *cv1* `U`” and `T2` is “pointer to member of `C2` of type *cv2* `U`”, for some non-function type `U`, where `C1` is reference-related to `C2` or `C2` is reference-related to `C1` ([[dcl.init.ref]](../dcl/dcl.init.ref.md)), the cv-combined type of `T2` and `T1` or the cv-combined type of `T1` and `T2`, respectively;
- if `T1` and `T2` are similar types ([[conv.qual]](conv.qual.md)), the cv-combined type of `T1` and `T2`;
- otherwise, a program that necessitates the determination of a composite pointer type is ill-formed.

[*Example 1*:

```cpp
typedef void *p;
typedef const int *q;
typedef int **pi;
typedef const int **pci;
```

The composite pointer type of `p` and `q` is “pointer to `const void`”; the composite pointer type of `pi` and `pci` is “pointer to `const` pointer to `const int`”. — *end example*]
