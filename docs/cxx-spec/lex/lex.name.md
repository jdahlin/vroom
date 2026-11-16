# 5.10 Identifiers [lex.name]

[Contents](../README.md) · up: [5 Lexical conventions \[lex\]](lex.md) · prev: [5.9 Preprocessing numbers \[lex.ppnumber\]](lex.ppnumber.md) · next: [5.11 Keywords \[lex.key\]](lex.key.md)

```bnf
identifier:
    identifier-nondigit
    identifier identifier-nondigit
    identifier digit
```

```bnf
identifier-nondigit:
    nondigit
    universal-character-name
```

```bnf
nondigit: one of
    'a b c d e f g h i j k l m'
    'n o p q r s t u v w x y z'
    'A B C D E F G H I J K L M'
    'N O P Q R S T U V W X Y Z _'
```

```bnf
digit: one of
    '0 1 2 3 4 5 6 7 8 9'
```

**1** An identifier is an arbitrarily long sequence of letters and digits. Each *universal-character-name* in an identifier shall designate a character whose encoding in ISO/IEC 10646 falls into one of the ranges specified in [Table 2](lex.name.md#tab:lex.name.allowed). The initial element shall not be a *universal-character-name* designating a character whose encoding falls into one of the ranges specified in [Table 3](lex.name.md#tab:lex.name.disallowed). Upper- and lower-case letters are different. All characters are significant.[^1]

<a id="tab:lex.name.allowed"></a>
**Table 2: Ranges of characters allowed** [tab:lex.name.allowed]

|  |  |  |  |  |
| --- | --- | --- | --- | --- |
| `00A8` | `00AA` | `00AD` | `00AF` | `00B2-00B5` |
| `00B7-00BA` | `00BC-00BE` | `00C0-00D6` | `00D8-00F6` | `00F8-00FF` |
| `0100-167F` | `1681-180D` | `180F-1FFF` |  |  |
| `200B-200D` | `202A-202E` | `203F-2040` | `2054` | `2060-206F` |
| `2070-218F` | `2460-24FF` | `2776-2793` | `2C00-2DFF` | `2E80-2FFF` |
| `3004-3007` | `3021-302F` | `3031-D7FF` |  |  |
| `F900-FD3D` | `FD40-FDCF` | `FDF0-FE44` | `FE47-FFFD` |  |
| `10000-1FFFD` | `20000-2FFFD` | `30000-3FFFD` | `40000-4FFFD` | `50000-5FFFD` |
| `60000-6FFFD` | `70000-7FFFD` | `80000-8FFFD` | `90000-9FFFD` | `A0000-AFFFD` |
| `B0000-BFFFD` | `C0000-CFFFD` | `D0000-DFFFD` | `E0000-EFFFD` |  |

<a id="tab:lex.name.disallowed"></a>
**Table 3: Ranges of characters disallowed initially (combining characters)** [tab:lex.name.disallowed]

|  |  |  |  |
| --- | --- | --- | --- |
| `0300-036F` | `1DC0-1DFF` | `20D0-20FF` | `FE20-FE2F` |

**2** The identifiers in [Table 4](lex.name.md#tab:lex.name.special) have a special meaning when appearing in a certain context. When referred to in the grammar, these identifiers are used explicitly rather than using the *identifier* grammar production. Unless otherwise specified, any ambiguity as to whether a given *identifier* has a special meaning is resolved to interpret the token as a regular *identifier*.

<a id="tab:lex.name.special"></a>
**Table 4: Identifiers with special meaning** [tab:lex.name.special]

|  |
| --- |
| `final` |
| `import` |
| `module` |
| `override` |

**3** In addition, some identifiers are reserved for use by C++ implementations and shall not be used otherwise; no diagnostic is required.

- Each identifier that contains a double underscore `__` or begins with an underscore followed by an uppercase letter is reserved to the implementation for any use.
- Each identifier that begins with an underscore is reserved to the implementation for use as a name in the global namespace.

[^1]: On systems in which linkers cannot accept extended characters, an encoding of the *universal-character-name* may be used in forming valid external identifiers. For example, some otherwise unused character or sequence of characters may be used to encode the `\u` in a *universal-character-name*. Extended characters may produce a long external identifier, but C++ does not place a translation limit on significant characters for external identifiers. In C++, upper- and lower-case letters are considered different for all identifiers, including external identifiers.
