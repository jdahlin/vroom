# 21.2.3 `char_traits` specializations [char.traits.specializations]

[Contents](../README.md) · up: [21.2 Character traits \[char.traits\]](char.traits.md) · prev: [21.2.2 Traits typedefs \[char.traits.typedefs\]](char.traits.typedefs.md) · next: [21.2.3.1 `struct char_traits<char>` \[char.traits.specializations.char\]](char.traits.specializations.char.md)

```cpp
namespace std {
  template<> struct char_traits<char>;
  template<> struct char_traits<char8_t>;
  template<> struct char_traits<char16_t>;
  template<> struct char_traits<char32_t>;
  template<> struct char_traits<wchar_t>;
}
```

**1** The header `<string>` defines five specializations of the class template `char_traits`: `char_traits<char>`, `char_traits<char8_t>`, `char_traits<char16_t>`, `char_traits<char32_t>`, and `char_traits<wchar_t>`.

## Subclauses

- [21.2.3.1 `struct char_traits<char>` \[char.traits.specializations.char\]](char.traits.specializations.char.md)
- [21.2.3.2 `struct char_traits<char8_t>` \[char.traits.specializations.char8.t\]](char.traits.specializations.char8.t.md)
- [21.2.3.3 `struct char_traits<char16_t>` \[char.traits.specializations.char16.t\]](char.traits.specializations.char16.t.md)
- [21.2.3.4 `struct char_traits<char32_t>` \[char.traits.specializations.char32.t\]](char.traits.specializations.char32.t.md)
- [21.2.3.5 `struct char_traits<wchar_t>` \[char.traits.specializations.wchar.t\]](char.traits.specializations.wchar.t.md)
