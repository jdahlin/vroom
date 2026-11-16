# 9.4.2 Character arrays [dcl.init.string]

[Contents](README.md) · up: [9.4 Initializers \[dcl.init\]](dcl.init.md) · prev: [9.4.1 Aggregates \[dcl.init.aggr\]](dcl.init.aggr.md) · next: [9.4.3 References \[dcl.init.ref\]](dcl.init.ref.md)

**1** An array of ordinary character type ([[basic.fundamental]](basic.fundamental.md)), `char8_t` array, `char16_t` array, `char32_t` array, or `wchar_t` array can be initialized by an ordinary string literal, UTF-8 string literal, UTF-16 string literal, UTF-32 string literal, or wide string literal, respectively, or by an appropriately-typed *string-literal* enclosed in braces ([[lex.string]](lex.string.md)). Successive characters of the value of the *string-literal* initialize the elements of the array. [*Example 1*:

```cpp
char msg[] = "Syntax error on line %s\n";
```

shows a character array whose members are initialized with a *string-literal*. Note that because `'\n'` is a single character and because a trailing `'\0'` is appended, `sizeof(msg)` is `25`. — *end example*]

**2** There shall not be more initializers than there are array elements. [*Example 2*:

```cpp
char cv[4] = "asdf";            // error
```

is ill-formed since there is no space for the implied trailing `'\0'`. — *end example*]

**3** If there are fewer initializers than there are array elements, each element not explicitly initialized shall be zero-initialized ([[dcl.init]](dcl.init.md)).
