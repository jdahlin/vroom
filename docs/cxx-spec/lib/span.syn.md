# 22.7.2 Header `<span>` synopsis [span.syn]

[Contents](../README.md) · up: [22.7 Views \[views\]](views.md) · prev: [22.7.1 General \[views.general\]](views.general.md) · next: [22.7.3 Class template `span` \[views.span\]](views.span.md)

```cpp
namespace std {
  // constants
  inline constexpr size_t dynamic_extent = numeric_limits<size_t>::max();

  // [views.span], class template span
  template<class ElementType, size_t Extent = dynamic_extent>
    class span;

  template<class ElementType, size_t Extent>
    inline constexpr bool ranges::enable_view<span<ElementType, Extent>> =
      Extent == 0 || Extent == dynamic_extent;
  template<class ElementType, size_t Extent>
    inline constexpr bool ranges::enable_borrowed_range<span<ElementType, Extent>> = true;

  // [span.objectrep], views of object representation
  template<class ElementType, size_t Extent>
    span<const byte, Extent == dynamic_extent ? dynamic_extent : sizeof(ElementType) * Extent>
      as_bytes(span<ElementType, Extent> s) noexcept;

  template<class ElementType, size_t Extent>
    span<byte, Extent == dynamic_extent ? dynamic_extent : sizeof(ElementType) * Extent>
      as_writable_bytes(span<ElementType, Extent> s) noexcept;
}
```
