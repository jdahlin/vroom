# 21.4.7 Suffix for `basic_string_view` literals [string.view.literals]

[Contents](../README.md) · up: [21.4 String view classes \[string.view\]](string.view.md) · prev: [21.4.6 Hash support \[string.view.hash\]](string.view.hash.md) · next: [21.5 Null-terminated sequence utilities \[c.strings\]](c.strings.md)

```cpp
constexpr string_view operator""sv(const char* str, size_t len) noexcept;
```

**1** *Returns:* `string_view{str, len}`.

```cpp
constexpr u8string_view operator""sv(const char8_t* str, size_t len) noexcept;
```

**2** *Returns:* `u8string_view{str, len}`.

```cpp
constexpr u16string_view operator""sv(const char16_t* str, size_t len) noexcept;
```

**3** *Returns:* `u16string_view{str, len}`.

```cpp
constexpr u32string_view operator""sv(const char32_t* str, size_t len) noexcept;
```

**4** *Returns:* `u32string_view{str, len}`.

```cpp
constexpr wstring_view operator""sv(const wchar_t* str, size_t len) noexcept;
```

**5** *Returns:* `wstring_view{str, len}`.
