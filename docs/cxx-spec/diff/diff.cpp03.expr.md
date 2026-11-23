# C.4.2 [[expr]](../expr/expr.md): expressions [diff.cpp03.expr]

[Contents](../README.md) · up: [C.4 C++ and ISO C++ 2003 \[diff.cpp03\]](diff.cpp03.md) · prev: [C.4.1 [[lex]](../lex/lex.md): lexical conventions \[diff.cpp03.lex\]](diff.cpp03.lex.md) · next: [C.4.3 [[dcl.dcl]](../dcl/dcl.dcl.md): declarations \[diff.cpp03.dcl.dcl\]](diff.cpp03.dcl.dcl.md)

**1** **Affected subclause:** [[conv.ptr]](../expr/conv.ptr.md) <br>**Change:** Only literals are integer null pointer constants. <br>**Rationale:** Removing surprising interactions with templates and constant expressions. <br>**Effect on original feature:** Valid C++ 2003 code may fail to compile or produce different results in this International Standard. For example:

```cpp
void f(void *);     // #1
void f(...);        // #2
template<int N> void g() {
  f(0*N);           // calls #2; used to call #1
}
```

**2** **Affected subclause:** [[expr.mul]](../expr/expr.mul.md) <br>**Change:** Specify rounding for results of integer `/` and `%`. <br>**Rationale:** Increase portability, C99 compatibility. <br>**Effect on original feature:** Valid C++ 2003 code that uses integer division rounds the result toward 0 or toward negative infinity, whereas this International Standard always rounds the result toward 0.

**3** **Affected subclause:** [[expr.log.and]](../expr/expr.log.and.md) <br>**Change:** `&&` is valid in a *type-name*. <br>**Rationale:** Required for new features. <br>**Effect on original feature:** Valid C++ 2003 code may fail to compile or produce different results in this International Standard. For example:

```cpp
bool b1 = new int && false;             // previously false, now ill-formed
struct S { operator int(); };
bool b2 = &S::operator int && false;    // previously false, now ill-formed
```
