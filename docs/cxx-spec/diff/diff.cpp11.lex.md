# C.3.1 [[lex]](../lex/lex.md): lexical conventions [diff.cpp11.lex]

[Contents](../README.md) · up: [C.3 C++ and ISO C++ 2011 \[diff.cpp11\]](diff.cpp11.md) · prev: [C.3 C++ and ISO C++ 2011 \[diff.cpp11\]](diff.cpp11.md) · next: [C.3.2 [[basic]](../basic/basic.md): basics \[diff.cpp11.basic\]](diff.cpp11.basic.md)

**1** **Affected subclause:** [[lex.ppnumber]](../lex/lex.ppnumber.md) <br>**Change:** *pp-number* can contain one or more single quotes. <br>**Rationale:** Necessary to enable single quotes as digit separators. <br>**Effect on original feature:** Valid C++ 2011 code may fail to compile or may change meaning in this International Standard. For example, the following code is valid both in C++ 2011 and in this International Standard, but the macro invocation produces different outcomes because the single quotes delimit a *character-literal* in C++ 2011, whereas they are digit separators in this International Standard:

```cpp
#define M(x, ...) __VA_ARGS__
int x[2] = { M(1'2,3'4, 5) };
// int x[2] = { 5 }; --- C++ 2011
// int x[2] = { 3'4, 5 }; --- this International Standard
```
