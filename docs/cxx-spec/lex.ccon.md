# 5.13.3 Character literals [lex.ccon]

[Contents](README.md) · up: [5.13 Literals \[lex.literal\]](lex.literal.md) · prev: [5.13.2 Integer literals \[lex.icon\]](lex.icon.md) · next: [5.13.4 Floating-point literals \[lex.fcon\]](lex.fcon.md)

```bnf
character-literal:
    encoding-prefixₒₚₜ ''' c-char-sequence '''
```

```bnf
encoding-prefix: one of
    'u8' 'u' 'U' 'L'
```

```bnf
c-char-sequence:
    c-char
    c-char-sequence c-char
```

```bnf
c-char:
    any member of the basic source character set except the single-quote ''', backslash '\', or new-line character
    escape-sequence
    universal-character-name
```

```bnf
escape-sequence:
    simple-escape-sequence
    octal-escape-sequence
    hexadecimal-escape-sequence
```

```bnf
simple-escape-sequence: one of
    '\'' '\"' '\?' '\\'
    '\a' '\b' '\f' '\n' '\r' '\t' '\v'
```

```bnf
octal-escape-sequence:
    '\' octal-digit
    '\' octal-digit octal-digit
    '\' octal-digit octal-digit octal-digit
```

```bnf
hexadecimal-escape-sequence:
    '\x' hexadecimal-digit
    hexadecimal-escape-sequence hexadecimal-digit
```

**1** A *character-literal* that does not begin with `u8`, `u`, `U`, or `L` is an *ordinary character literal*. An ordinary character literal that contains a single *c-char* representable in the execution character set has type `char`, with value equal to the numerical value of the encoding of the *c-char* in the execution character set. An ordinary character literal that contains more than one *c-char* is a *multicharacter literal*. A multicharacter literal, or an ordinary character literal containing a single *c-char* not representable in the execution character set, is conditionally-supported, has type `int`, and has an implementation-defined value.

**2** A *character-literal* that begins with `u8`, such as `u8'w'`, is a *character-literal* of type `char8_t`, known as a *UTF-8 character literal*. The value of a UTF-8 character literal is equal to its ISO/IEC 10646 code point value, provided that the code point value can be encoded as a single UTF-8 code unit. [*Note 1*: That is, provided the code point value is in the range [0, 7*F*] (hexadecimal). — *end note*] If the value is not representable with a single UTF-8 code unit, the program is ill-formed. A UTF-8 character literal containing multiple *c-char*s is ill-formed.

**3** A *character-literal* that begins with the letter `u`, such as `u'x'`, is a *character-literal* of type `char16_t`, known as a *UTF-16 character literal*. The value of a UTF-16 character literal is equal to its ISO/IEC 10646 code point value, provided that the code point value is representable with a single 16-bit code unit. [*Note 2*: That is, provided the code point value is in the range [0, *FFFF*] (hexadecimal). — *end note*] If the value is not representable with a single 16-bit code unit, the program is ill-formed. A UTF-16 character literal containing multiple *c-char*s is ill-formed.

**4** A *character-literal* that begins with the letter `U`, such as `U'y'`, is a *character-literal* of type `char32_t`, known as a *UTF-32 character literal*. The value of a UTF-32 character literal containing a single *c-char* is equal to its ISO/IEC 10646 code point value. A UTF-32 character literal containing multiple *c-char*s is ill-formed.

**5** A *character-literal* that begins with the letter `L`, such as `L'z'`, is a *wide-character literal*. A wide-character literal has type `wchar_t`.[^1] The value of a wide-character literal containing a single *c-char* has value equal to the numerical value of the encoding of the *c-char* in the execution wide-character set, unless the *c-char* has no representation in the execution wide-character set, in which case the value is implementation-defined. [*Note 3*: The type `wchar_t` is able to represent all members of the execution wide-character set (see [[basic.fundamental]](basic.fundamental.md)). — *end note*] The value of a wide-character literal containing multiple *c-char*s is implementation-defined.

**6** Certain non-graphic characters, the single quote `'`, the double quote `"`, the question mark `?`,[^2] and the backslash `\`, can be represented according to [Table 9](lex.ccon.md#tab:lex.ccon.esc). The double quote `"` and the question mark `?`, can be represented as themselves or by the escape sequences `\"` and `\?` respectively, but the single quote `'` and the backslash `\` shall be represented by the escape sequences `\'` and `\\` respectively. Escape sequences in which the character following the backslash is not listed in [Table 9](lex.ccon.md#tab:lex.ccon.esc) are conditionally-supported, with implementation-defined semantics. An escape sequence specifies a single character.

<a id="tab:lex.ccon.esc"></a>
**Table 9: Escape sequences** [tab:lex.ccon.esc]

|  |  |  |
| --- | --- | --- |
| new-line | NL(LF) | `\n` |
| horizontal tab | HT | `\t` |
| vertical tab | VT | `\v` |
| backspace | BS | `\b` |
| carriage return | CR | `\r` |
| form feed | FF | `\f` |
| alert | BEL | `\a` |
| backslash | \\ | `\\` |
| question mark | ? | `\?` |
| single quote | `'` | `\'` |
| double quote | `"` | `\"` |
| octal number | *ooo* | `\ooo` |
| hex number | *hhh* | `\xhhh` |

**7** The escape `\ooo` consists of the backslash followed by one, two, or three octal digits that are taken to specify the value of the desired character. The escape `\xhhh` consists of the backslash followed by `x` followed by one or more hexadecimal digits that are taken to specify the value of the desired character. There is no limit to the number of digits in a hexadecimal sequence. A sequence of octal or hexadecimal digits is terminated by the first character that is not an octal digit or a hexadecimal digit, respectively. The value of a *character-literal* is implementation-defined if it falls outside of the implementation-defined range defined for `char` (for *character-literal*s with no prefix) or `wchar_t` (for *character-literal*s prefixed by `L`). [*Note 4*: If the value of a *character-literal* prefixed by `u`, `u8`, or `U` is outside the range defined for its type, the program is ill-formed. — *end note*]

**8** A *universal-character-name* is translated to the encoding, in the appropriate execution character set, of the character named. If there is no such encoding, the *universal-character-name* is translated to an implementation-defined encoding. [*Note 5*: In translation phase 1, a *universal-character-name* is introduced whenever an actual extended character is encountered in the source text. Therefore, all extended characters are described in terms of *universal-character-name*s. However, the actual compiler implementation may use its own native character set, so long as the same results are obtained. — *end note*]

[^1]: They are intended for character sets where a character does not fit into a single byte.
[^2]: Using an escape sequence for a question mark is supported for compatibility with ISO C++ 2014 and ISO C.
