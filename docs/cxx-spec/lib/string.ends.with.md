# 21.3.2.7.6 `basic_string::ends_with` [string.ends.with]

[Contents](../README.md) · up: [21.3.2.7 String operations \[string.ops\]](string.ops.md) · prev: [21.3.2.7.5 `basic_string::starts_with` \[string.starts.with\]](string.starts.with.md) · next: [21.3.3 Non-member functions \[string.nonmembers\]](string.nonmembers.md)

```cpp
constexpr bool ends_with(basic_string_view<charT, traits> x) const noexcept;
constexpr bool ends_with(charT x) const noexcept;
constexpr bool ends_with(const charT* x) const;
```

**1** *Effects:* Equivalent to:

```cpp
return basic_string_view<charT, traits>(data(), size()).ends_with(x);
```
