# 21.4.6 Hash support [string.view.hash]

[Contents](../README.md) · up: [21.4 String view classes \[string.view\]](string.view.md) · prev: [21.4.5 Inserters and extractors \[string.view.io\]](string.view.io.md) · next: [21.4.7 Suffix for `basic_string_view` literals \[string.view.literals\]](string.view.literals.md)

```cpp
template<> struct hash<string_view>;
template<> struct hash<u8string_view>;
template<> struct hash<u16string_view>;
template<> struct hash<u32string_view>;
template<> struct hash<wstring_view>;
```

**1** The specialization is enabled ([[unord.hash]](unord.hash.md)). [*Note 1*: The hash value of a string view object is equal to the hash value of the corresponding string object ([[basic.string.hash]](basic.string.hash.md)). — *end note*]
