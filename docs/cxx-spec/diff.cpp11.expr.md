# C.3.3 [[expr]](expr.md): expressions [diff.cpp11.expr]

[Contents](README.md) · up: [C.3 C++ and ISO C++ 2011 \[diff.cpp11\]](diff.cpp11.md) · prev: [C.3.2 [[basic]](basic.md): basics \[diff.cpp11.basic\]](diff.cpp11.basic.md) · next: [C.3.4 [[dcl.dcl]](dcl.dcl.md): declarations \[diff.cpp11.dcl.dcl\]](diff.cpp11.dcl.dcl.md)

**1** **Affected subclause:** [[expr.cond]](expr.cond.md) <br>**Change:** A conditional expression with a throw expression as its second or third operand keeps the type and value category of the other operand. <br>**Rationale:** Formerly mandated conversions (lvalue-to-rvalue ([[conv.lval]](conv.lval.md)), array-to-pointer ([[conv.array]](conv.array.md)), and function-to-pointer ([[conv.func]](conv.func.md)) standard conversions), especially the creation of the temporary due to lvalue-to-rvalue conversion, were considered gratuitous and surprising. <br>**Effect on original feature:** Valid C++ 2011 code that relies on the conversions may behave differently in this International Standard:

```cpp
struct S {
  int x = 1;
  void mf() { x = 2; }
};
int f(bool cond) {
  S s;
  (cond ? s : throw 0).mf();
  return s.x;
}
```

In C++ 2011, `f(true)` returns `1`. In this International Standard, it returns `2`.

```cpp
sizeof(true ? "" : throw 0)
```

In C++ 2011, the expression yields `sizeof(const char*)`. In this International Standard, it yields `sizeof(const char[1])`.
