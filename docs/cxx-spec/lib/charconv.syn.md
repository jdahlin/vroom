# 20.19.1 Header `<charconv>` synopsis [charconv.syn]

[Contents](../README.md) · up: [20.19 Primitive numeric conversions \[charconv\]](charconv.md) · prev: [20.19 Primitive numeric conversions \[charconv\]](charconv.md) · next: [20.19.2 Primitive numeric output conversion \[charconv.to.chars\]](charconv.to.chars.md)

```cpp
@%
@namespace std {
  // floating-point format for primitive numerical conversion
  enum class chars_format {
    scientific = unspecified,
    fixed = unspecified,
    hex = unspecified,
    general = fixed | scientific
  };
@%
@
  // [charconv.to.chars], primitive numerical output conversion
  struct to_chars_result {
    char* ptr;
    errc ec;
    friend bool operator==(const to_chars_result&, const to_chars_result&) = default;
  };

  to_chars_result to_chars(char* first, char* last, see below value, int base = 10);
  to_chars_result to_chars(char* first, char* last, bool value, int base = 10) = delete;

  to_chars_result to_chars(char* first, char* last, float value);
  to_chars_result to_chars(char* first, char* last, double value);
  to_chars_result to_chars(char* first, char* last, long double value);

  to_chars_result to_chars(char* first, char* last, float value, chars_format fmt);
  to_chars_result to_chars(char* first, char* last, double value, chars_format fmt);
  to_chars_result to_chars(char* first, char* last, long double value, chars_format fmt);

  to_chars_result to_chars(char* first, char* last, float value,
                           chars_format fmt, int precision);
  to_chars_result to_chars(char* first, char* last, double value,
                           chars_format fmt, int precision);
  to_chars_result to_chars(char* first, char* last, long double value,
                           chars_format fmt, int precision);
@%
@
  // [charconv.from.chars], primitive numerical input conversion
  struct from_chars_result {
    const char* ptr;
    errc ec;
    friend bool operator==(const from_chars_result&, const from_chars_result&) = default;
  };

  from_chars_result from_chars(const char* first, const char* last,
                               see below& value, int base = 10);

  from_chars_result from_chars(const char* first, const char* last, float& value,
                               chars_format fmt = chars_format::general);
  from_chars_result from_chars(const char* first, const char* last, double& value,
                               chars_format fmt = chars_format::general);
  from_chars_result from_chars(const char* first, const char* last, long double& value,
                               chars_format fmt = chars_format::general);
}
```

**1** The type `chars_format` is a bitmask type ([[bitmask.types]](bitmask.types.md)) with elements `scientific`, `fixed`, and `hex`.

**2** The types `to_chars_result` and `from_chars_result` have the data members and special members specified above. They have no base classes or members other than those specified.
