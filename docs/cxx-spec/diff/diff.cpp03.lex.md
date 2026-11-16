# C.4.1 [[lex]](../lex/lex.md): lexical conventions [diff.cpp03.lex]

[Contents](../README.md) · up: [C.4 C++ and ISO C++ 2003 \[diff.cpp03\]](diff.cpp03.md) · prev: [C.4 C++ and ISO C++ 2003 \[diff.cpp03\]](diff.cpp03.md) · next: [C.4.2 [[expr]](../expr/expr.md): expressions \[diff.cpp03.expr\]](diff.cpp03.expr.md)

**1** **Affected subclause:** [[lex.pptoken]](../lex/lex.pptoken.md) <br>**Change:** New kinds of *string-literal*s. <br>**Rationale:** Required for new features. <br>**Effect on original feature:** Valid C++ 2003 code may fail to compile or produce different results in this International Standard. Specifically, macros named `R`, `u8`, `u8R`, `u`, `uR`, `U`, `UR`, or `LR` will not be expanded when adjacent to a *string-literal* but will be interpreted as part of the *string-literal*. For example:

```cpp
#define u8 "abc"
const char* s = u8"def";        // Previously "abcdef", now "def"
```

**2** **Affected subclause:** [[lex.pptoken]](../lex/lex.pptoken.md) <br>**Change:** User-defined literal string support. <br>**Rationale:** Required for new features. <br>**Effect on original feature:** Valid C++ 2003 code may fail to compile or produce different results in this International Standard. For example:

```cpp
#define _x "there"
"hello"_x           // #1
```

Previously, #1 would have consisted of two separate preprocessing tokens and the macro `_x` would have been expanded. In this International Standard, #1 consists of a single preprocessing token, so the macro is not expanded.

**3** **Affected subclause:** [[lex.key]](../lex/lex.key.md) <br>**Change:** New keywords. <br>**Rationale:** Required for new features. <br>**Effect on original feature:** Added to [Table 5](../lex/lex.key.md#tab:lex.key), the following identifiers are new keywords: `alignas`, `alignof`, `char16_t`, `char32_t`, `constexpr`, `decltype`, `noexcept`, `nullptr`, `static_assert`, and `thread_local`. Valid C++ 2003 code using these identifiers is invalid in this International Standard.

**4** **Affected subclause:** [[lex.icon]](../lex/lex.icon.md) <br>**Change:** Type of integer literals. <br>**Rationale:** C99 compatibility. <br>**Effect on original feature:** Certain integer literals larger than can be represented by `long` could change from an unsigned integer type to `signed long long`.
