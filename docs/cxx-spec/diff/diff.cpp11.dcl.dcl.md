# C.3.4 [[dcl.dcl]](../dcl/dcl.dcl.md): declarations [diff.cpp11.dcl.dcl]

[Contents](../README.md) · up: [C.3 C++ and ISO C++ 2011 \[diff.cpp11\]](diff.cpp11.md) · prev: [C.3.3 [[expr]](../expr/expr.md): expressions \[diff.cpp11.expr\]](diff.cpp11.expr.md) · next: [C.3.5 [[library]](../lib/library/library.md): library introduction \[diff.cpp11.library\]](diff.cpp11.library.md)

**1** **Affected subclause:** [[dcl.constexpr]](../dcl/dcl.constexpr.md) <br>**Change:** `constexpr` non-static member functions are not implicitly `const` member functions. <br>**Rationale:** Necessary to allow `constexpr` member functions to mutate the object. <br>**Effect on original feature:** Valid C++ 2011 code may fail to compile in this International Standard. For example, the following code is valid in C++ 2011 but invalid in this International Standard because it declares the same member function twice with different return types:

```cpp
struct S {
  constexpr const int &f();
  int &f();
};
```

**2** **Affected subclause:** [[dcl.init.aggr]](../dcl/dcl.init.aggr.md) <br>**Change:** Classes with default member initializers can be aggregates. <br>**Rationale:** Necessary to allow default member initializers to be used by aggregate initialization. <br>**Effect on original feature:** Valid C++ 2011 code may fail to compile or may change meaning in this International Standard. For example:

```cpp
struct S {          // Aggregate in C++ 2014 onwards.
  int m = 1;
};
struct X {
  operator int();
  operator S();
};
X a{};
S b{a};             // uses copy constructor in C++ 2011,
                    // performs aggregate initialization in this International Standard
```
