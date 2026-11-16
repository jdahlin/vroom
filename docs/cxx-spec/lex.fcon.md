# 5.13.4 Floating-point literals [lex.fcon]

[Contents](README.md) · up: [5.13 Literals \[lex.literal\]](lex.literal.md) · prev: [5.13.3 Character literals \[lex.ccon\]](lex.ccon.md) · next: [5.13.5 String literals \[lex.string\]](lex.string.md)

```bnf
floating-point-literal:
    decimal-floating-point-literal
    hexadecimal-floating-point-literal
```

```bnf
decimal-floating-point-literal:
    fractional-constant exponent-partₒₚₜ floating-point-suffixₒₚₜ
    digit-sequence exponent-part floating-point-suffixₒₚₜ
```

```bnf
hexadecimal-floating-point-literal:
    hexadecimal-prefix hexadecimal-fractional-constant binary-exponent-part floating-point-suffixₒₚₜ
    hexadecimal-prefix hexadecimal-digit-sequence binary-exponent-part floating-point-suffixₒₚₜ
```

```bnf
fractional-constant:
    digit-sequenceₒₚₜ '.' digit-sequence
    digit-sequence '.'
```

```bnf
hexadecimal-fractional-constant:
    hexadecimal-digit-sequenceₒₚₜ '.' hexadecimal-digit-sequence
    hexadecimal-digit-sequence '.'
```

```bnf
exponent-part:
    'e' signₒₚₜ digit-sequence
    'E' signₒₚₜ digit-sequence
```

```bnf
binary-exponent-part:
    'p' signₒₚₜ digit-sequence
    'P' signₒₚₜ digit-sequence
```

```bnf
sign: one of
    '+ -'
```

```bnf
digit-sequence:
    digit
    digit-sequence '''ₒₚₜ digit
```

```bnf
floating-point-suffix: one of
    'f l F L'
```

**1** The type of a *floating-point-literal* is determined by its *floating-point-suffix* as specified in [Table 10](lex.fcon.md#tab:lex.fcon.type).

<a id="tab:lex.fcon.type"></a>
**Table 10: Types of *floating-point-literal*s** [tab:lex.fcon.type]

| *floating-point-suffix* | type |
| --- | --- |
| none | `double` |
| `f` or `F` | `float` |
| `l` or `L` | `long` `double` |

**2** The *significand* of a *floating-point-literal* is the *fractional-constant* or *digit-sequence* of a *decimal-floating-point-literal* or the *hexadecimal-fractional-constant* or *hexadecimal-digit-sequence* of a *hexadecimal-floating-point-literal*. In the significand, the sequence of *digit*s or *hexadecimal-digit*s and optional period are interpreted as a base *N* real number *s*, where *N* is 10 for a *decimal-floating-point-literal* and 16 for a *hexadecimal-floating-point-literal*. [*Note 1*: Any optional separating single quotes are ignored when determining the value. — *end note*] If an *exponent-part* or *binary-exponent-part* is present, the exponent *e* of the *floating-point-literal* is the result of interpreting the sequence of an optional *sign* and the *digit*s as a base 10 integer. Otherwise, the exponent *e* is 0. The scaled value of the literal is *s* × 10<sup>*e*</sup> for a *decimal-floating-point-literal* and *s* × 2<sup>*e*</sup> for a *hexadecimal-floating-point-literal*. [*Example 1*: The *floating-point-literal*s `49.625` and `0xC.68p+2` have the same value. The *floating-point-literal*s `1.602'176'565e-19` and `1.602176565e-19` have the same value. — *end example*]

**3** If the scaled value is not in the range of representable values for its type, the program is ill-formed. Otherwise, the value of a *floating-point-literal* is the scaled value if representable, else the larger or smaller representable value nearest the scaled value, chosen in an implementation-defined manner.
