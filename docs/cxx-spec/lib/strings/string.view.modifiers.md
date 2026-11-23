# 21.4.2.5 Modifiers [string.view.modifiers]

[Contents](../../README.md) · up: [21.4.2 Class template `basic_string_view` \[string.view.template\]](string.view.template.md) · prev: [21.4.2.4 Element access \[string.view.access\]](string.view.access.md) · next: [21.4.2.6 String operations \[string.view.ops\]](string.view.ops.md)

```cpp
constexpr void remove_prefix(size_type n);
```

**1** *Preconditions:* `n <= size()`.

**2** *Effects:* Equivalent to: `data_ += n; size_ -= n;`

```cpp
constexpr void remove_suffix(size_type n);
```

**3** *Preconditions:* `n <= size()`.

**4** *Effects:* Equivalent to: `size_ -= n;`

```cpp
constexpr void swap(basic_string_view& s) noexcept;
```

**5** *Effects:* Exchanges the values of `*this` and `s`.
