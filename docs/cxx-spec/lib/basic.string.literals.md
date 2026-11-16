# 21.3.6 Suffix for `basic_string` literals [basic.string.literals]

[Contents](../README.md) · up: [21.3 String classes \[string.classes\]](string.classes.md) · prev: [21.3.5 Hash support \[basic.string.hash\]](basic.string.hash.md) · next: [21.4 String view classes \[string.view\]](string.view.md)

```cpp
constexpr string operator""s(const char* str, size_t len);
```

**1** *Returns:* `string{str, len}`.

```cpp
constexpr u8string operator""s(const char8_t* str, size_t len);
```

**2** *Returns:* `u8string{str, len}`.

```cpp
constexpr u16string operator""s(const char16_t* str, size_t len);
```

**3** *Returns:* `u16string{str, len}`.

```cpp
constexpr u32string operator""s(const char32_t* str, size_t len);
```

**4** *Returns:* `u32string{str, len}`.

```cpp
constexpr wstring operator""s(const wchar_t* str, size_t len);
```

**5** *Returns:* `wstring{str, len}`.

**6** [*Note 1*: The same suffix `s` is used for `chrono::duration` literals denoting seconds but there is no conflict, since duration suffixes apply to numbers and string literal suffixes apply to character array literals. — *end note*]
