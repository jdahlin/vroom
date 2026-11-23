# 5.13.2 Integer literals [lex.icon]

[Contents](../README.md) · up: [5.13 Literals \[lex.literal\]](lex.literal.md) · prev: [5.13.1 Kinds of literals \[lex.literal.kinds\]](lex.literal.kinds.md) · next: [5.13.3 Character literals \[lex.ccon\]](lex.ccon.md)

```bnf
integer-literal:
    binary-literal integer-suffixₒₚₜ
    octal-literal integer-suffixₒₚₜ
    decimal-literal integer-suffixₒₚₜ
    hexadecimal-literal integer-suffixₒₚₜ
```

```bnf
binary-literal:
    '0b' binary-digit
    '0B' binary-digit
    binary-literal '''ₒₚₜ binary-digit
```

```bnf
octal-literal:
    '0'
    octal-literal '''ₒₚₜ octal-digit
```

```bnf
decimal-literal:
    nonzero-digit
    decimal-literal '''ₒₚₜ digit
```

```bnf
hexadecimal-literal:
    hexadecimal-prefix hexadecimal-digit-sequence
```

```bnf
binary-digit: one of
    '0 1'
```

```bnf
octal-digit: one of
    '0 1 2 3 4 5 6 7'
```

```bnf
nonzero-digit: one of
    '1 2 3 4 5 6 7 8 9'
```

```bnf
hexadecimal-prefix: one of
    '0x 0X'
```

```bnf
hexadecimal-digit-sequence:
    hexadecimal-digit
    hexadecimal-digit-sequence '''ₒₚₜ hexadecimal-digit
```

```bnf
hexadecimal-digit: one of
    '0 1 2 3 4 5 6 7 8 9'
    'a b c d e f'
    'A B C D E F'
```

```bnf
integer-suffix:
    unsigned-suffix long-suffixₒₚₜ
    unsigned-suffix long-long-suffixₒₚₜ
    long-suffix unsigned-suffixₒₚₜ
    long-long-suffix unsigned-suffixₒₚₜ
```

```bnf
unsigned-suffix: one of
    'u U'
```

```bnf
long-suffix: one of
    'l L'
```

```bnf
long-long-suffix: one of
    'll LL'
```

**1** In an *integer-literal*, the sequence of *binary-digit*s, *octal-digit*s, *digit*s, or *hexadecimal-digit*s is interpreted as a base *N* integer as shown in table [Table 7](lex.icon.md#tab:lex.icon.base); the lexically first digit of the sequence of digits is the most significant. [*Note 1*: The prefix and any optional separating single quotes are ignored when determining the value. — *end note*]

<a id="tab:lex.icon.base"></a>
**Table 7: Base of *integer-literal*s** [tab:lex.icon.base]

| Kind of *integer-literal* | base *N* |
| --- | --- |
| *binary-literal* | 2 |
| *octal-literal* | 8 |
| *decimal-literal* | 10 |
| *hexadecimal-literal* | 16 |

**2** The *hexadecimal-digit*s `a` through `f` and `A` through `F` have decimal values ten through fifteen. [*Example 1*: The number twelve can be written `12`, `014`, `0XC`, or `0b1100`. The *integer-literal*s `1048576`, `1'048'576`, `0X100000`, `0x10'0000`, and `0'004'000'000` all have the same value. — *end example*]

**3** The type of an *integer-literal* is the first type in the list in [Table 8](lex.icon.md#tab:lex.icon.type) corresponding to its optional *integer-suffix* in which its value can be represented. An *integer-literal* is a prvalue.

<a id="tab:lex.icon.type"></a>
**Table 8: Types of *integer-literal*s** [tab:lex.icon.type]

| *integer-suffix* | *decimal-literal* | *integer-literal* other than *decimal-literal* |
| --- | --- | --- |
| none | `int` | `int` |
|  | `long int` | `unsigned int` |
|  | `long long int` | `long int` |
|  |  | `unsigned long int` |
|  |  | `long long int` |
|  |  | `unsigned long long int` |
| `u` or `U` | `unsigned int` | `unsigned int` |
|  | `unsigned long int` | `unsigned long int` |
|  | `unsigned long long int` | `unsigned long long int` |
| `l` or `L` | `long int` | `long int` |
|  | `long long int` | `unsigned long int` |
|  |  | `long long int` |
|  |  | `unsigned long long int` |
| Both `u` or `U` | `unsigned long int` | `unsigned long int` |
| and `l` or `L` | `unsigned long long int` | `unsigned long long int` |
| `ll` or `LL` | `long long int` | `long long int` |
|  |  | `unsigned long long int` |
| Both `u` or `U` | `unsigned long long int` | `unsigned long long int` |
| and `ll` or `LL` |  |  |

**4** If an *integer-literal* cannot be represented by any type in its list and an extended integer type ([[basic.fundamental]](../basic/basic.fundamental.md)) can represent its value, it may have that extended integer type. If all of the types in the list for the *integer-literal* are signed, the extended integer type shall be signed. If all of the types in the list for the *integer-literal* are unsigned, the extended integer type shall be unsigned. If the list contains both signed and unsigned types, the extended integer type may be signed or unsigned. A program is ill-formed if one of its translation units contains an *integer-literal* that cannot be represented by any of the allowed types.
