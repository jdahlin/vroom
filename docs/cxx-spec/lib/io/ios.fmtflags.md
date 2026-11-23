# 29.5.3.1.2 Type `ios_base::fmtflags` [ios.fmtflags]

[Contents](../../README.md) · up: [29.5.3.1 Types \[ios.types\]](ios.types.md) · prev: [29.5.3.1.1 Class `ios_base::failure` \[ios.failure\]](ios.failure.md) · next: [29.5.3.1.3 Type `ios_base::iostate` \[ios.iostate\]](ios.iostate.md)

```cpp
using fmtflags = T1;
```

**1** The type `fmtflags` is a bitmask type ([[bitmask.types]](../library/bitmask.types.md)). Setting its elements has the effects indicated in [Table 116](ios.fmtflags.md#tab:ios.fmtflags).

<a id="tab:ios.fmtflags"></a>
**Table 116: `fmtflags` effects** [tab:ios.fmtflags]

| Element | Effect(s) if set |
| --- | --- |
| `boolalpha` | insert and extract `bool` type in alphabetic format |
| `dec` | converts integer input or generates integer output in decimal base |
| `fixed` | generate floating-point output in fixed-point notation |
| `hex` | converts integer input or generates integer output in hexadecimal base |
| `internal` | adds fill characters at a designated internal point in certain generated output, or identical to `right` if no such point is designated |
| `left` | adds fill characters on the right (final positions) of certain generated output |
| `oct` | converts integer input or generates integer output in octal base |
| `right` | adds fill characters on the left (initial positions) of certain generated output |
| `scientific` | generates floating-point output in scientific notation |
| `showbase` | generates a prefix indicating the numeric base of generated integer output |
| `showpoint` | generates a decimal-point character unconditionally in generated floating-point output |
| `showpos` | generates a `+` sign in non-negative generated numeric output |
| `skipws` | skips leading whitespace before certain input operations |
| `unitbuf` | flushes output after each output operation |
| `uppercase` | replaces certain lowercase letters with their uppercase equivalents in generated output |

**2** Type `fmtflags` also defines the constants indicated in [Table 117](ios.fmtflags.md#tab:ios.fmtflags.const).

<a id="tab:ios.fmtflags.const"></a>
**Table 117: `fmtflags` constants** [tab:ios.fmtflags.const]

| Constant | Allowable values |
| --- | --- |
| `adjustfield` | `left \| right \| internal` |
| `basefield` | `dec \| oct \| hex` |
| `floatfield` | `scientific \| fixed` |
