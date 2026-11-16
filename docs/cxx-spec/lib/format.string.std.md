# 20.20.2.2 Standard format specifiers [format.string.std]

[Contents](../README.md) · up: [20.20.2 Format string \[format.string\]](format.string.md) · prev: [20.20.2.1 In general \[format.string.general\]](format.string.general.md) · next: [20.20.3 Error reporting \[format.err.report\]](format.err.report.md)

**1** Each `formatter` specializations described in [[format.formatter.spec]](format.formatter.spec.md) for fundamental and string types interprets *format-spec* as a *std-format-spec*. [*Note 1*: The format specification can be used to specify such details as field width, alignment, padding, and decimal precision. Some of the formatting options are only supported for arithmetic types. — *end note*] The syntax of format specifications is as follows:

```bnf
std-format-spec:
    fill-and-alignₒₚₜ signₒₚₜ '#'ₒₚₜ '0'ₒₚₜ widthₒₚₜ precisionₒₚₜ 'L'ₒₚₜ typeₒₚₜ
```

```bnf
fill-and-align:
    fillₒₚₜ align
```

```bnf
fill:
    any character other than '{' or '}'
```

```bnf
align: one of
    '< > ^'
```

```bnf
sign: one of
    '+ -' space
```

```bnf
width:
    positive-integer
    '{' arg-idₒₚₜ '}'
```

```bnf
precision:
    '.' nonnegative-integer
    '.' '{' arg-idₒₚₜ '}'
```

```bnf
type: one of
    'a A b B c d e E f F g G o p s x X'
```

**2** [*Note 2*: The *fill* character can be any character other than `{` or `}`. The presence of a fill character is signaled by the character following it, which must be one of the alignment options. If the second character of *std-format-spec* is not a valid alignment option, then it is assumed that both the fill character and the alignment option are absent. — *end note*]

**3** The *align* specifier applies to all argument types. The meaning of the various alignment options is as specified in [Table 59](format.string.std.md#tab:format.align). [*Example 1*:

```cpp
char c = 120;
string s0 = format("{:6}", 42);         // value of s0 is " 42"
string s1 = format("{:6}", 'x');        // value of s1 is "x "
string s2 = format("{:*<6}", 'x');      // value of s2 is "x*****"
string s3 = format("{:*>6}", 'x');      // value of s3 is "*****x"
string s4 = format("{:*^6}", 'x');      // value of s4 is "**x***"
string s5 = format("{:6d}", c);         // value of s5 is " 120"
string s6 = format("{:6}", true);       // value of s6 is "true "
```

— *end example*] [*Note 3*: Unless a minimum field width is defined, the field width is determined by the size of the content and the alignment option has no effect. — *end note*]

<a id="tab:format.align"></a>
**Table 59: Meaning of *align* options** [tab:format.align]

| Option | Meaning |
| --- | --- |
| `<` | Forces the field to be aligned to the start of the available space. This is the default for non-arithmetic types, `charT`, and `bool`, unless an integer presentation type is specified. |
| `>` | Forces the field to be aligned to the end of the available space. This is the default for arithmetic types other than `charT` and `bool` or when an integer presentation type is specified. |
| `^` | Forces the field to be centered within the available space by inserting $\bigl\lfloor \frac{n}{2} \bigr\rfloor$ characters before and $\bigl\lceil \frac{n}{2} \bigr\rceil$ characters after the value, where *n* is the total number of fill characters to insert. |

**4** The *sign* option is only valid for arithmetic types other than `charT` and `bool` or when an integer presentation type is specified. The meaning of the various options is as specified in [Table 60](format.string.std.md#tab:format.sign).

<a id="tab:format.sign"></a>
**Table 60: Meaning of *sign* options** [tab:format.sign]

| Option | Meaning |
| --- | --- |
| `+` | Indicates that a sign should be used for both non-negative and negative numbers. The `+` sign is inserted before the output of `to_chars` for non-negative numbers other than negative zero. [*Note 4*: For negative numbers and negative zero the output of `to_chars` will already contain the sign so no additional transformation is performed. — *end note*] |
| `-` | Indicates that a sign should be used for negative numbers and negative zero only (this is the default behavior). |
| space | Indicates that a leading space should be used for non-negative numbers other than negative zero, and a minus sign for negative numbers and negative zero. |

**5** The *sign* option applies to floating-point infinity and NaN. [*Example 2*:

```cpp
double inf = numeric_limits<double>::infinity();
double nan = numeric_limits<double>::quiet_NaN();
string s0 = format("{0:},{0:+},{0:-},{0: }", 1);        // value of s0 is "1,+1,1, 1"
string s1 = format("{0:},{0:+},{0:-},{0: }", -1);       // value of s1 is "-1,-1,-1,-1"
string s2 = format("{0:},{0:+},{0:-},{0: }", inf);      // value of s2 is "inf,+inf,inf, inf"
string s3 = format("{0:},{0:+},{0:-},{0: }", nan);      // value of s3 is "nan,+nan,nan, nan"
```

— *end example*]

**6** The `#` option causes the *alternate form* to be used for the conversion. This option is valid for arithmetic types other than `charT` and `bool` or when an integer presentation type is specified, and not otherwise. For integral types, the alternate form inserts the base prefix (if any) specified in [Table 62](format.string.std.md#tab:format.type.int) into the output after the sign character (possibly space) if there is one, or before the output of `to_chars` otherwise. For floating-point types, the alternate form causes the result of the conversion of finite values to always contain a decimal-point character, even if no digits follow it. Normally, a decimal-point character appears in the result of these conversions only if a digit follows it. In addition, for `g` and `G` conversions, trailing zeros are not removed from the result.

**7** If `{ arg-idₒₚₜ }` is used in a *width* or *precision*, the value of the corresponding formatting argument is used in its place. If the corresponding formatting argument is not of integral type, or its value is negative for *precision* or non-positive for *width*, an exception of type `format_error` is thrown.

**8** The *positive-integer* in *width* is a decimal integer defining the minimum field width. If *width* is not specified, there is no minimum field width, and the field width is determined based on the content of the field.

**9** The *width* of a string is defined as the estimated number of column positions appropriate for displaying it in a terminal. [*Note 5*: This is similar to the semantics of the POSIX `wcswidth` function. — *end note*]

**10** For the purposes of width computation, a string is assumed to be in a locale-independent, implementation-defined encoding. Implementations should use a Unicode encoding on platforms capable of displaying Unicode text in a terminal. [*Note 6*: This is the case for Windows-based and many POSIX-based operating systems. — *end note*]

**11** For a string in a Unicode encoding, implementations should estimate the width of a string as the sum of estimated widths of the first code points in its extended grapheme clusters. The extended grapheme clusters of a string are defined by UAX #29. The estimated width of the following code points is 2:

- `U+1100-U+115F`
- `U+2329-U+232A`
- `U+2E80-U+303E`
- `U+3040-U+A4CF`
- `U+AC00-U+D7A3`
- `U+F900-U+FAFF`
- `U+FE10-U+FE19`
- `U+FE30-U+FE6F`
- `U+FF00-U+FF60`
- `U+FFE0-U+FFE6`
- `U+1F300-U+1F64F`
- `U+1F900-U+1F9FF`
- `U+20000-U+2FFFD`
- `U+30000-U+3FFFD`

The estimated width of other code points is 1.

**12** For a string in a non-Unicode encoding, the width of a string is unspecified.

**13** A zero (`0`) character preceding the *width* field pads the field with leading zeros (following any indication of sign or base) to the field width, except when applied to an infinity or NaN. This option is only valid for arithmetic types other than `charT` and `bool` or when an integer presentation type is specified. If the `0` character and an *align* option both appear, the `0` character is ignored. [*Example 3*:

```cpp
char c = 120;
string s1 = format("{:+06d}", c);       // value of s1 is "+00120"
string s2 = format("{:#06x}", 0xa);     // value of s2 is "0x000a"
string s3 = format("{:<06}", -42);      // value of s3 is "-42 " (0 is ignored because of < alignment)
```

— *end example*]

**14** The *nonnegative-integer* in *precision* is a decimal integer defining the precision or maximum field size. It can only be used with floating-point and string types. For floating-point types this field specifies the formatting precision. For string types, this field provides an upper bound for the estimated width of the prefix of the input string that is copied into the output. For a string in a Unicode encoding, the formatter copies to the output the longest prefix of whole extended grapheme clusters whose estimated width is no greater than the precision.

**15** When the `L` option is used, the form used for the conversion is called the *locale-specific form*. The `L` option is only valid for arithmetic types, and its effect depends upon the type.

- For integral types, the locale-specific form causes the context’s locale to be used to insert the appropriate digit group separator characters.
- For floating-point types, the locale-specific form causes the context’s locale to be used to insert the appropriate digit group and radix separator characters.
- For the textual representation of `bool`, the locale-specific form causes the context’s locale to be used to insert the appropriate string as if obtained with `numpunct::truename` or `numpunct::falsename`.

**16** The *type* determines how the data should be presented.

**17** The available string presentation types are specified in [Table 61](format.string.std.md#tab:format.type.string).

<a id="tab:format.type.string"></a>
**Table 61: Meaning of *type* options for strings** [tab:format.type.string]

| Type | Meaning |
| --- | --- |
| none, `s` | Copies the string to the output. |

**18** The meaning of some non-string presentation types is defined in terms of a call to `to_chars`. In such cases, let `[first, last)` be a range large enough to hold the `to_chars` output and `value` be the formatting argument value. Formatting is done as if by calling `to_chars` as specified and copying the output through the output iterator of the format context. [*Note 7*: Additional padding and adjustments are performed prior to copying the output through the output iterator as specified by the format specifiers. — *end note*]

**19** The available integer presentation types for integral types other than `bool` and `charT` are specified in [Table 62](format.string.std.md#tab:format.type.int). [*Example 4*:

```cpp
string s0 = format("{}", 42);                           // value of s0 is "42"
string s1 = format("{0:b} {0:d} {0:o} {0:x}", 42);      // value of s1 is "101010 42 52 2a"
string s2 = format("{0:#x} {0:#X}", 42);                // value of s2 is "0x2a 0X2A"
string s3 = format("{:L}", 1234);                       // value of s3 might be "1,234"
                                                        // (depending on the locale)
```

— *end example*]

<a id="tab:format.type.int"></a>
**Table 62: Meaning of *type* options for integer types** [tab:format.type.int]

| Type | Meaning |
| --- | --- |
| `b` | `to_chars(first, last, value, 2)`; the base prefix is `0b`. |
| `B` | The same as `b`, except that the base prefix is `0B`. |
| `c` | Copies the character `static_cast<charT>(value)` to the output. Throws `format_error` if `value` is not in the range of representable values for `charT`. |
| `d` | `to_chars(first, last, value)`. |
| `o` | `to_chars(first, last, value, 8)`; the base prefix is `0` if `value` is nonzero and is empty otherwise. |
| `x` | `to_chars(first, last, value, 16)`; the base prefix is `0x`. |
| `X` | The same as `x`, except that it uses uppercase letters for digits above 9 and the base prefix is `0X`. |
| none | The same as `d`. [*Note 8*: If the formatting argument type is `charT` or `bool`, the default is instead `c` or `s`, respectively. — *end note*] |

**20** The available `charT` presentation types are specified in [Table 63](format.string.std.md#tab:format.type.char).

<a id="tab:format.type.char"></a>
**Table 63: Meaning of *type* options for `charT`** [tab:format.type.char]

| Type | Meaning |
| --- | --- |
| none, `c` | Copies the character to the output. |
| `b`, `B`, `d`, `o`, `x`, `X` | As specified in [Table 62](format.string.std.md#tab:format.type.int). |

**21** The available `bool` presentation types are specified in [Table 64](format.string.std.md#tab:format.type.bool).

<a id="tab:format.type.bool"></a>
**Table 64: Meaning of *type* options for `bool`** [tab:format.type.bool]

| Type | Meaning |
| --- | --- |
| none, `s` | Copies textual representation, either `true` or `false`, to the output. |
| `b`, `B`, `c`, `d`, `o`, `x`, `X` | As specified in [Table 62](format.string.std.md#tab:format.type.int) for the value `static_cast<unsigned char>(value)`. |

**22** The available floating-point presentation types and their meanings for values other than infinity and NaN are specified in [Table 65](format.string.std.md#tab:format.type.float). For lower-case presentation types, infinity and NaN are formatted as `inf` and `nan`, respectively. For upper-case presentation types, infinity and NaN are formatted as `INF` and `NAN`, respectively. [*Note 9*: In either case, a sign is included if indicated by the *sign* option. — *end note*]

<a id="tab:format.type.float"></a>
**Table 65: Meaning of *type* options for floating-point types** [tab:format.type.float]

| Type | Meaning |
| --- | --- |
| `a` | If *precision* is specified, equivalent to<br>`to_chars(first, last, value, chars_format::hex, precision)`<br>where `precision` is the specified formatting precision; equivalent to<br>`to_chars(first, last, value, chars_format::hex)`<br>otherwise. |
| `A` | The same as `a`, except that it uses uppercase letters for digits above 9 and `P` to indicate the exponent. |
| `e` | Equivalent to<br>`to_chars(first, last, value, chars_format::scientific, precision)`<br>where `precision` is the specified formatting precision, or `6` if *precision* is not specified. |
| `E` | The same as `e`, except that it uses `E` to indicate exponent. |
| `f`, `F` | Equivalent to<br>`to_chars(first, last, value, chars_format::fixed, precision)`<br>where `precision` is the specified formatting precision, or `6` if *precision* is not specified. |
| `g` | Equivalent to<br>`to_chars(first, last, value, chars_format::general, precision)`<br>where `precision` is the specified formatting precision, or `6` if *precision* is not specified. |
| `G` | The same as `g`, except that it uses `E` to indicate exponent. |
| none | If *precision* is specified, equivalent to<br>`to_chars(first, last, value, chars_format::general, precision)`<br>where `precision` is the specified formatting precision; equivalent to<br>`to_chars(first, last, value)`<br>otherwise. |

**23** The available pointer presentation types and their mapping to `to_chars` are specified in [Table 66](format.string.std.md#tab:format.type.ptr). [*Note 10*: Pointer presentation types also apply to `nullptr_t`. — *end note*]

<a id="tab:format.type.ptr"></a>
**Table 66: Meaning of *type* options for pointer types** [tab:format.type.ptr]

| Type | Meaning |
| --- | --- |
| none, `p` | If `uintptr_t` is defined,<br>`to_chars(first, last, reinterpret_cast<uintptr_t>(value), 16)`<br>with the prefix `0x` added to the output; otherwise, implementation-defined. |
