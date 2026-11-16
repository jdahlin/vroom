# C.2.4 [[class]](class.md): classes [diff.cpp14.class]

[Contents](README.md) · up: [C.2 C++ and ISO C++ 2014 \[diff.cpp14\]](diff.cpp14.md) · prev: [C.2.3 [[dcl.dcl]](dcl.dcl.md): declarations \[diff.cpp14.dcl.dcl\]](diff.cpp14.dcl.dcl.md) · next: [C.2.5 [[temp]](temp.md): templates \[diff.cpp14.temp\]](diff.cpp14.temp.md)

**1** **Affected subclause:** [[class.inhctor.init]](class.inhctor.init.md) <br>**Change:** Inheriting a constructor no longer injects a constructor into the derived class. <br>**Rationale:** Better interaction with other language features. <br>**Effect on original feature:** Valid C++ 2014 code that uses inheriting constructors may not be valid or may have different semantics. A *using-declaration* that names a constructor now makes the corresponding base class constructors visible to initializations of the derived class rather than declaring additional derived class constructors.

```cpp
struct A {
  template<typename T> A(T, typename T::type = 0);
  A(int);
};
struct B : A {
  using A::A;
  B(int);
};
B b(42L);           // now calls B(int), used to call B<long>(long),
                    // which called A(int) due to substitution failure
                    // in A<long>(long).
```
