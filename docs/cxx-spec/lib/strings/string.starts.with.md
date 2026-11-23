# 21.3.2.7.5 `basic_string::starts_with` [string.starts.with]

[Contents](../../README.md) · up: [21.3.2.7 String operations \[string.ops\]](string.ops.md) · prev: [21.3.2.7.4 `basic_string::compare` \[string.compare\]](string.compare.md) · next: [21.3.2.7.6 `basic_string::ends_with` \[string.ends.with\]](string.ends.with.md)

```cpp
constexpr bool starts_with(basic_string_view<charT, traits> x) const noexcept;
constexpr bool starts_with(charT x) const noexcept;
constexpr bool starts_with(const charT* x) const;
```

**1** *Effects:* Equivalent to:

```cpp
return basic_string_view<charT, traits>(data(), size()).starts_with(x);
```
