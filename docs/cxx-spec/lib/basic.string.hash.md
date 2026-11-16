# 21.3.5 Hash support [basic.string.hash]

[Contents](../README.md) · up: [21.3 String classes \[string.classes\]](string.classes.md) · prev: [21.3.4 Numeric conversions \[string.conversions\]](string.conversions.md) · next: [21.3.6 Suffix for `basic_string` literals \[basic.string.literals\]](basic.string.literals.md)

```cpp
template<> struct hash<string>;
template<> struct hash<u8string>;
template<> struct hash<u16string>;
template<> struct hash<u32string>;
template<> struct hash<wstring>;
template<> struct hash<pmr::string>;
template<> struct hash<pmr::u8string>;
template<> struct hash<pmr::u16string>;
template<> struct hash<pmr::u32string>;
template<> struct hash<pmr::wstring>;
```

**1** If `S` is one of these string types, `SV` is the corresponding string view type, and `s` is an object of type `S`, then `hash<S>()(s) == hash<SV>()(SV(s))`.
