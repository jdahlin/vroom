# C.4.3 [[dcl.dcl]](../dcl/dcl.dcl.md): declarations [diff.cpp03.dcl.dcl]

[Contents](../README.md) · up: [C.4 C++ and ISO C++ 2003 \[diff.cpp03\]](diff.cpp03.md) · prev: [C.4.2 [[expr]](../expr/expr.md): expressions \[diff.cpp03.expr\]](diff.cpp03.expr.md) · next: [C.4.4 [[class]](../class/class.md): classes \[diff.cpp03.class\]](diff.cpp03.class.md)

**1** **Affected subclause:** [[dcl.spec]](../dcl/dcl.spec.md) <br>**Change:** Remove `auto` as a storage class specifier. <br>**Rationale:** New feature. <br>**Effect on original feature:** Valid C++ 2003 code that uses the keyword `auto` as a storage class specifier may be invalid in this International Standard. In this International Standard, `auto` indicates that the type of a variable is to be deduced from its initializer expression.

**2** **Affected subclause:** [[dcl.init.list]](../dcl/dcl.init.list.md) <br>**Change:** Narrowing restrictions in aggregate initializers. <br>**Rationale:** Catches bugs. <br>**Effect on original feature:** Valid C++ 2003 code may fail to compile in this International Standard. For example, the following code is valid in C++ 2003 but invalid in this International Standard because `double` to `int` is a narrowing conversion:

```cpp
int x[] = { 2.0 };
```
