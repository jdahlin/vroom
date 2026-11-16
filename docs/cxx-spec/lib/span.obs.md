# 22.7.3.5 Observers [span.obs]

[Contents](../README.md) · up: [22.7.3 Class template `span` \[views.span\]](views.span.md) · prev: [22.7.3.4 Subviews \[span.sub\]](span.sub.md) · next: [22.7.3.6 Element access \[span.elem\]](span.elem.md)

```cpp
constexpr size_type size() const noexcept;
```

**1** *Effects:* Equivalent to: `return size_;`

```cpp
constexpr size_type size_bytes() const noexcept;
```

**2** *Effects:* Equivalent to: `return size() * sizeof(element_type);`

```cpp
[[nodiscard]] constexpr bool empty() const noexcept;
```

**3** *Effects:* Equivalent to: `return size() == 0;`
