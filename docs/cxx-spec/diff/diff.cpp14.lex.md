# C.2.1 [[lex]](../lex/lex.md): lexical conventions [diff.cpp14.lex]

[Contents](../README.md) · up: [C.2 C++ and ISO C++ 2014 \[diff.cpp14\]](diff.cpp14.md) · prev: [C.2 C++ and ISO C++ 2014 \[diff.cpp14\]](diff.cpp14.md) · next: [C.2.2 [[expr]](../expr/expr.md): expressions \[diff.cpp14.expr\]](diff.cpp14.expr.md)

**1** **Affected subclause:** [[lex.phases]](../lex/lex.phases.md) <br>**Change:** Removal of trigraph support as a required feature. <br>**Rationale:** Prevents accidental uses of trigraphs in non-raw string literals and comments. <br>**Effect on original feature:** Valid C++ 2014 code that uses trigraphs may not be valid or may have different semantics in this International Standard. Implementations may choose to translate trigraphs as specified in C++ 2014 if they appear outside of a raw string literal, as part of the implementation-defined mapping from physical source file characters to the basic source character set.

**2** **Affected subclause:** [[lex.ppnumber]](../lex/lex.ppnumber.md) <br>**Change:** *pp-number* can contain `p` *sign* and `P` *sign*. <br>**Rationale:** Necessary to enable *hexadecimal-floating-point-literal*s. <br>**Effect on original feature:** Valid C++ 2014 code may fail to compile or produce different results in this International Standard. Specifically, character sequences like `0p+0` and `0e1_p+0` are three separate tokens each in C++ 2014, but one single token in this International Standard. For example:

```cpp
#define F(a) b ## a
int b0p = F(0p+0);  // ill-formed; equivalent to ``int b0p = b0p + 0;'' in C++ 2014
```
