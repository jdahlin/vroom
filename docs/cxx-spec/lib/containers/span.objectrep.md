# 22.7.3.8 Views of object representation [span.objectrep]

[Contents](../../README.md) · up: [22.7.3 Class template `span` \[views.span\]](views.span.md) · prev: [22.7.3.7 Iterator support \[span.iterators\]](span.iterators.md) · next: [23 Iterators library \[iterators\]](../iterators/iterators.md)

```cpp
template<class ElementType, size_t Extent>
  span<const byte, Extent == dynamic_extent ? dynamic_extent : sizeof(ElementType) * Extent>
    as_bytes(span<ElementType, Extent> s) noexcept;
```

**1** *Effects:* Equivalent to: `return R{reinterpret_cast<const byte*>(s.data()), s.size_bytes()};` where `R` is the return type.

```cpp
template<class ElementType, size_t Extent>
  span<byte, Extent == dynamic_extent ? dynamic_extent : sizeof(ElementType) * Extent>
    as_writable_bytes(span<ElementType, Extent> s) noexcept;
```

**2** *Constraints:* `is_const_v<ElementType>` is `false`.

**3** *Effects:* Equivalent to: `return R{reinterpret_cast<byte*>(s.data()), s.size_bytes()};` where `R` is the return type.
