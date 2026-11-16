# 5.13.5 String literals [lex.string]

[Contents](../README.md) · up: [5.13 Literals \[lex.literal\]](lex.literal.md) · prev: [5.13.4 Floating-point literals \[lex.fcon\]](lex.fcon.md) · next: [5.13.6 Boolean literals \[lex.bool\]](lex.bool.md)

```bnf
string-literal:
    encoding-prefixₒₚₜ '"' s-char-sequenceₒₚₜ '"'
    encoding-prefixₒₚₜ 'R' raw-string
```

```bnf
s-char-sequence:
    s-char
    s-char-sequence s-char
```

```bnf
s-char:
    any member of the basic source character set except the double-quote '"', backslash '\', or new-line character
    escape-sequence
    universal-character-name
```

```bnf
raw-string:
    '"' d-char-sequenceₒₚₜ '(' r-char-sequenceₒₚₜ ')' d-char-sequenceₒₚₜ '"'
```

```bnf
r-char-sequence:
    r-char
    r-char-sequence r-char
```

```bnf
r-char:
    any member of the source character set, except a right parenthesis ')' followed by
        the initial d-char-sequence (which may be empty) followed by a double quote '"'.
```

```bnf
d-char-sequence:
    d-char
    d-char-sequence d-char
```

```bnf
d-char:
    any member of the basic source character set except:
        space, the left parenthesis '(', the right parenthesis ')', the backslash '\', and the control characters
        representing horizontal tab, vertical tab, form feed, and newline.
```

**1** A *string-literal* that has an `R` in the prefix is a *raw string literal*. The *d-char-sequence* serves as a delimiter. The terminating *d-char-sequence* of a *raw-string* is the same sequence of characters as the initial *d-char-sequence*. A *d-char-sequence* shall consist of at most 16 characters.

**2** [*Note 1*: The characters `'('` and `')'` are permitted in a *raw-string*. Thus, `R"delimiter((a|b))delimiter"` is equivalent to `"(a|b)"`. — *end note*]

**3** [*Note 2*: A source-file new-line in a raw string literal results in a new-line in the resulting execution string literal. Assuming no whitespace at the beginning of lines in the following example, the assert will succeed:

```cpp
const char* p = R"(a\
b
c)";
assert(std::strcmp(p, "a\\\nb\nc") == 0);
```

— *end note*]

**4** [*Example 1*: The raw string

```cpp
R"a(
)\
a"
)a"
```

is equivalent to `"\n)\\\na\"\n"`. The raw string

```cpp
R"(x = "\"y\"")"
```

is equivalent to `"x = \"\\\"y\\\"\""`. — *end example*]

**5** After translation phase 6, a *string-literal* that does not begin with an *encoding-prefix* is an *ordinary string literal*. An ordinary string literal has type “array of *n* `const char`” where *n* is the size of the string as defined below, has static storage duration ([[basic.stc]](../basic/basic.stc.md)), and is initialized with the given characters.

**6** A *string-literal* that begins with `u8`, such as `u8"asdf"`, is a *UTF-8 string literal*. A UTF-8 string literal has type “array of *n* `const char8_t`”, where *n* is the size of the string as defined below; each successive element of the object representation ([[basic.types]](../basic/basic.types.md)) has the value of the corresponding code unit of the UTF-8 encoding of the string.

**7** Ordinary string literals and UTF-8 string literals are also referred to as narrow string literals.

**8** A *string-literal* that begins with `u`, such as `u"asdf"`, is a *UTF-16 string literal*. A UTF-16 string literal has type “array of *n* `const char16_t`”, where *n* is the size of the string as defined below; each successive element of the array has the value of the corresponding code unit of the UTF-16 encoding of the string. [*Note 3*: A single *c-char* may produce more than one `char16_t` character in the form of surrogate pairs. A surrogate pair is a representation for a single code point as a sequence of two 16-bit code units. — *end note*]

**9** A *string-literal* that begins with `U`, such as `U"asdf"`, is a *UTF-32 string literal*. A UTF-32 string literal has type “array of *n* `const char32_t`”, where *n* is the size of the string as defined below; each successive element of the array has the value of the corresponding code unit of the UTF-32 encoding of the string.

**10** A *string-literal* that begins with `L`, such as `L"asdf"`, is a *wide string literal*. A wide string literal has type “array of *n* `const wchar_t`”, where *n* is the size of the string as defined below; it is initialized with the given characters.

**11** In translation phase 6 ([[lex.phases]](lex.phases.md)), adjacent *string-literal*s are concatenated. If both *string-literal*s have the same *encoding-prefix*, the resulting concatenated *string-literal* has that *encoding-prefix*. If one *string-literal* has no *encoding-prefix*, it is treated as a *string-literal* of the same *encoding-prefix* as the other operand. If a UTF-8 string literal token is adjacent to a wide string literal token, the program is ill-formed. Any other concatenations are conditionally-supported with implementation-defined behavior. [*Note 4*: This concatenation is an interpretation, not a conversion. Because the interpretation happens in translation phase 6 (after each character from a *string-literal* has been translated into a value from the appropriate character set), a *string-literal*’s initial rawness has no effect on the interpretation or well-formedness of the concatenation. — *end note*] [Table 11](lex.string.md#tab:lex.string.concat) has some examples of valid concatenations.

<a id="tab:lex.string.concat"></a>
**Table 11: String literal concatenations** [tab:lex.string.concat]

|  |  |  |  |  |  |  |  |  |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Source | Means | Source | Means | Source | Means |  |  |  |
| `u"a"` | `u"b"` | `u"ab"` | `U"a"` | `U"b"` | `U"ab"` | `L"a"` | `L"b"` | `L"ab"` |
| `u"a"` | `"b"` | `u"ab"` | `U"a"` | `"b"` | `U"ab"` | `L"a"` | `"b"` | `L"ab"` |
| `"a"` | `u"b"` | `u"ab"` | `"a"` | `U"b"` | `U"ab"` | `"a"` | `L"b"` | `L"ab"` |

Characters in concatenated strings are kept distinct.

[*Example 2*:

```cpp
"\xA" "B"
```

contains the two characters `'\xA'` and `'B'` after concatenation (and not the single hexadecimal character `'\xAB'`). — *end example*]

**12** After any necessary concatenation, in translation phase 7 ([[lex.phases]](lex.phases.md)), `'\0'` is appended to every *string-literal* so that programs that scan a string can find its end.

**13** Escape sequences and *universal-character-name*s in non-raw string literals have the same meaning as in *character-literal*s ([[lex.ccon]](lex.ccon.md)), except that the single quote `'` is representable either by itself or by the escape sequence `\'`, and the double quote `"` shall be preceded by a `\`, and except that a *universal-character-name* in a UTF-16 string literal may yield a surrogate pair. In a narrow string literal, a *universal-character-name* may map to more than one `char` or `char8_t` element due to *multibyte encoding*. The size of a `char32_t` or wide string literal is the total number of escape sequences, *universal-character-name*s, and other characters, plus one for the terminating `U'\0'` or `L'\0'`. The size of a UTF-16 string literal is the total number of escape sequences, *universal-character-name*s, and other characters, plus one for each character requiring a surrogate pair, plus one for the terminating `u'\0'`. [*Note 5*: The size of a `char16_t` string literal is the number of code units, not the number of characters. — *end note*] [*Note 6*: Any *universal-character-name*s are required to correspond to a code point in the range [0, *D*800) or [*E*000, 10*FFFF*] (hexadecimal) ([[lex.charset]](lex.charset.md)). — *end note*] The size of a narrow string literal is the total number of escape sequences and other characters, plus at least one for the multibyte encoding of each *universal-character-name*, plus one for the terminating `'\0'`.

**14** Evaluating a *string-literal* results in a string literal object with static storage duration, initialized from the given characters as specified above. Whether all *string-literal*s are distinct (that is, are stored in nonoverlapping objects) and whether successive evaluations of a *string-literal* yield the same or a different object is unspecified. [*Note 7*: The effect of attempting to modify a *string-literal* is undefined. — *end note*]
