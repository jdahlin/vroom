# C.2.5 [[temp]](temp.md): templates [diff.cpp14.temp]

[Contents](README.md) · up: [C.2 C++ and ISO C++ 2014 \[diff.cpp14\]](diff.cpp14.md) · prev: [C.2.4 [[class]](class.md): classes \[diff.cpp14.class\]](diff.cpp14.class.md) · next: [C.2.6 [[except]](except.md): exception handling \[diff.cpp14.except\]](diff.cpp14.except.md)

**1** **Affected subclause:** [[temp.deduct.type]](temp.deduct.type.md) <br>**Change:** Allowance to deduce from the type of a non-type template argument. <br>**Rationale:** In combination with the ability to declare non-type template arguments with placeholder types, allows partial specializations to decompose from the type deduced for the non-type template argument. <br>**Effect on original feature:** Valid C++ 2014 code may fail to compile or produce different results in this International Standard. For example:

```cpp
template <int N> struct A;
template <typename T, T N> int foo(A<N> *) = delete;
void foo(void *);
void bar(A<0> *p) {
  foo(p);           // ill-formed; previously well-formed
}
```
