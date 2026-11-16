# 21.3.2.6.7 `basic_string::copy` [string.copy]

[Contents](../README.md) · up: [21.3.2.6 Modifiers \[string.modifiers\]](string.modifiers.md) · prev: [21.3.2.6.6 `basic_string::replace` \[string.replace\]](string.replace.md) · next: [21.3.2.6.8 `basic_string::swap` \[string.swap\]](string.swap.md)

```cpp
constexpr size_type copy(charT* s, size_type n, size_type pos = 0) const;
```

**1** *Effects:* Equivalent to: `return basic_string_view<charT, traits>(*this).copy(s, n, pos);` [*Note 1*: This does not terminate `s` with a null object. — *end note*]
