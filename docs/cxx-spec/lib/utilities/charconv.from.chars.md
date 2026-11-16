# 20.19.3 Primitive numeric input conversion [charconv.from.chars]

[Contents](../../README.md) · up: [20.19 Primitive numeric conversions \[charconv\]](charconv.md) · prev: [20.19.2 Primitive numeric output conversion \[charconv.to.chars\]](charconv.to.chars.md) · next: [20.20 Formatting \[format\]](format.md)

**1** All functions named `from_chars` analyze the string `[first, last)` for a pattern, where `[first, last)` is required to be a valid range. If no characters match the pattern, `value` is unmodified, the member `ptr` of the return value is `first` and the member `ec` is equal to `errc::invalid_argument`. [*Note 1*: If the pattern allows for an optional sign, but the string has no digit characters following the sign, no characters match the pattern. — *end note*] Otherwise, the characters matching the pattern are interpreted as a representation of a value of the type of `value`. The member `ptr` of the return value points to the first character not matching the pattern, or has the value `last` if all characters match. If the parsed value is not in the range representable by the type of `value`, `value` is unmodified and the member `ec` of the return value is equal to `errc::result_out_of_range`. Otherwise, `value` is set to the parsed value, after rounding according to `round_to_nearest` ([[round.style]](../support/round.style.md)), and the member `ec` is value-initialized.

```cpp
from_chars_result from_chars(const char* first, const char* last,
                             see below& value, int base = 10);
```

**2** *Preconditions:* `base` has a value between 2 and 36 (inclusive).

**3** *Effects:* The pattern is the expected form of the subject sequence in the `"C"` locale for the given nonzero base, as described for `strtol`, except that no `"0x"` or `"0X"` prefix shall appear if the value of `base` is 16, and except that `'-'` is the only sign that may appear, and only if `value` has a signed type.

**4** *Throws:* Nothing.

**5** *Remarks:* The implementation shall provide overloads for all signed and unsigned integer types and `char` as the referenced type of the parameter `value`.

```cpp
from_chars_result from_chars(const char* first, const char* last, float& value,
                             chars_format fmt = chars_format::general);
from_chars_result from_chars(const char* first, const char* last, double& value,
                             chars_format fmt = chars_format::general);
from_chars_result from_chars(const char* first, const char* last, long double& value,
                             chars_format fmt = chars_format::general);
```

**6** *Preconditions:* `fmt` has the value of one of the enumerators of `chars_format`.

**7** *Effects:* The pattern is the expected form of the subject sequence in the `"C"` locale, as described for `strtod`, except that

- the sign `'+'` may only appear in the exponent part;
- if `fmt` has `chars_format::scientific` set but not `chars_format::fixed`, the otherwise optional exponent part shall appear;
- if `fmt` has `chars_format::fixed` set but not `chars_format::scientific`, the optional exponent part shall not appear; and
- if `fmt` is `chars_format::hex`, the prefix `"0x"` or `"0X"` is assumed. [*Example 1*: The string `0x123` is parsed to have the value `0` with remaining characters `x123`. — *end example*]

In any case, the resulting `value` is one of at most two floating-point values closest to the value of the string matching the pattern.

**8** *Throws:* Nothing.

*See also:* ISO C 7.22.1.3, 7.22.1.4
