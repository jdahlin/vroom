# 20.19.2 Primitive numeric output conversion [charconv.to.chars]

[Contents](../README.md) · up: [20.19 Primitive numeric conversions \[charconv\]](charconv.md) · prev: [20.19.1 Header `<charconv>` synopsis \[charconv.syn\]](charconv.syn.md) · next: [20.19.3 Primitive numeric input conversion \[charconv.from.chars\]](charconv.from.chars.md)

**1** All functions named `to_chars` convert `value` into a character string by successively filling the range `[first, last)`, where `[first, last)` is required to be a valid range. If the member `ec` of the return value is such that the value is equal to the value of a value-initialized `errc`, the conversion was successful and the member `ptr` is the one-past-the-end pointer of the characters written. Otherwise, the member `ec` has the value `errc::value_too_large`, the member `ptr` has the value `last`, and the contents of the range `[first, last)` are unspecified.

**2** The functions that take a floating-point `value` but not a `precision` parameter ensure that the string representation consists of the smallest number of characters such that there is at least one digit before the radix point (if present) and parsing the representation using the corresponding `from_chars` function recovers `value` exactly. [*Note 1*: This guarantee applies only if `to_chars` and `from_chars` are executed on the same implementation. — *end note*] If there are several such representations, the representation with the smallest difference from the floating-point argument value is chosen, resolving any remaining ties using rounding according to `round_to_nearest` ([[round.style]](round.style.md)).

**3** The functions taking a `chars_format` parameter determine the conversion specifier for `printf` as follows: The conversion specifier is `f` if `fmt` is `chars_format::fixed`, `e` if `fmt` is `chars_format::scientific`, `a` (without leading `"0x"` in the result) if `fmt` is `chars_format::hex`, and `g` if `fmt` is `chars_format::general`.

```cpp
to_chars_result to_chars(char* first, char* last, see below value, int base = 10);
```

**4** *Preconditions:* `base` has a value between 2 and 36 (inclusive).

**5** *Effects:* The value of `value` is converted to a string of digits in the given base (with no redundant leading zeroes). Digits in the range 10..35 (inclusive) are represented as lowercase characters `a`..`z`. If `value` is less than zero, the representation starts with `'-'`.

**6** *Throws:* Nothing.

**7** *Remarks:* The implementation shall provide overloads for all signed and unsigned integer types and `char` as the type of the parameter `value`.

```cpp
to_chars_result to_chars(char* first, char* last, float value);
to_chars_result to_chars(char* first, char* last, double value);
to_chars_result to_chars(char* first, char* last, long double value);
```

**8** *Effects:* `value` is converted to a string in the style of `printf` in the `"C"` locale. The conversion specifier is `f` or `e`, chosen according to the requirement for a shortest representation (see above); a tie is resolved in favor of `f`.

**9** *Throws:* Nothing.

```cpp
to_chars_result to_chars(char* first, char* last, float value, chars_format fmt);
to_chars_result to_chars(char* first, char* last, double value, chars_format fmt);
to_chars_result to_chars(char* first, char* last, long double value, chars_format fmt);
```

**10** *Preconditions:* `fmt` has the value of one of the enumerators of `chars_format`.

**11** *Effects:* `value` is converted to a string in the style of `printf` in the `"C"` locale.

**12** *Throws:* Nothing.

```cpp
to_chars_result to_chars(char* first, char* last, float value,
                         chars_format fmt, int precision);
to_chars_result to_chars(char* first, char* last, double value,
                         chars_format fmt, int precision);
to_chars_result to_chars(char* first, char* last, long double value,
                         chars_format fmt, int precision);
```

**13** *Preconditions:* `fmt` has the value of one of the enumerators of `chars_format`.

**14** *Effects:* `value` is converted to a string in the style of `printf` in the `"C"` locale with the given precision.

**15** *Throws:* Nothing.

*See also:* ISO C 7.21.6.1
