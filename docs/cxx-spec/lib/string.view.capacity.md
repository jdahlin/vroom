# 21.4.2.3 Capacity [string.view.capacity]

[Contents](../README.md) · up: [21.4.2 Class template `basic_string_view` \[string.view.template\]](string.view.template.md) · prev: [21.4.2.2 Iterator support \[string.view.iterators\]](string.view.iterators.md) · next: [21.4.2.4 Element access \[string.view.access\]](string.view.access.md)

```cpp
constexpr size_type size() const noexcept;
constexpr size_type length() const noexcept;
```

**1** *Returns:* `size_`.

```cpp
constexpr size_type max_size() const noexcept;
```

**2** *Returns:* The largest possible number of char-like objects that can be referred to by a `basic_string_view`.

```cpp
[[nodiscard]] constexpr bool empty() const noexcept;
```

**3** *Returns:* `size_ == 0`.
