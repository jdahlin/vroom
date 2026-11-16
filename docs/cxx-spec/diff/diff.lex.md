# C.5.1 [[lex]](../lex/lex.md): lexical conventions [diff.lex]

[Contents](../README.md) · up: [C.5 C++ and ISO C \[diff.iso\]](diff.iso.md) · prev: [C.5 C++ and ISO C \[diff.iso\]](diff.iso.md) · next: [C.5.2 [[basic]](../basic/basic.md): basics \[diff.basic\]](diff.basic.md)

**1** **Affected subclause:** [[lex.key]](../lex/lex.key.md) <br>**Change:** New Keywords<br> New keywords are added to C++; see [[lex.key]](../lex/lex.key.md). <br>**Rationale:** These keywords were added in order to implement the new semantics of C++. <br>**Effect on original feature:** Change to semantics of well-defined feature. Any ISO C programs that used any of these keywords as identifiers are not valid C++ programs. <br>**Difficulty of converting:** Syntactic transformation. Converting one specific program is easy. Converting a large collection of related programs takes more work. <br>**How widely used:** Common.

**2** **Affected subclause:** [[lex.ccon]](../lex/lex.ccon.md) <br>**Change:** Type of *character-literal* is changed from `int` to `char`. <br>**Rationale:** This is needed for improved overloaded function argument type matching. For example:

```cpp
int function( int i );
int function( char c );

function( 'x' );
```

It is preferable that this call match the second version of function rather than the first. <br>**Effect on original feature:** Change to semantics of well-defined feature. ISO C programs which depend on

```cpp
sizeof('x') == sizeof(int)
```

will not work the same as C++ programs. <br>**Difficulty of converting:** Simple. <br>**How widely used:** Programs which depend upon `sizeof('x')` are probably rare.

**3** **Affected subclause:** [[lex.string]](../lex/lex.string.md) <br>**Change:** String literals made const.<br> The type of a *string-literal* is changed from “array of `char`” to “array of `const char`”. The type of a UTF-8 string literal is changed from “array of `char`” to “array of `const char8_t`”. The type of a UTF-16 string literal is changed from “array of *some-integer-type*” to “array of `const char16_t`”. The type of a UTF-32 string literal is changed from “array of *some-integer-type*” to “array of `const char32_t`”. The type of a wide string literal is changed from “array of `wchar_t`” to “array of `const wchar_t`”. <br>**Rationale:** This avoids calling an inappropriate overloaded function, which might expect to be able to modify its argument. <br>**Effect on original feature:** Change to semantics of well-defined feature. <br>**Difficulty of converting:** Syntactic transformation. The fix is to add a cast:

```cpp
char* p = "abc";                // valid in C, invalid in C++
void f(char*) {
  char* p = (char*)"abc";       // OK: cast added
  f(p);
  f((char*)"def");              // OK: cast added
}
```

**How widely used:** Programs that have a legitimate reason to treat string literal objects as potentially modifiable memory are probably rare.
