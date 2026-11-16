# 24.6.1.2 Class template `empty_view` [range.empty.view]

[Contents](../../README.md) · up: [24.6.1 Empty view \[range.empty\]](range.empty.md) · prev: [24.6.1.1 Overview \[range.empty.overview\]](range.empty.overview.md) · next: [24.6.2 Single view \[range.single\]](range.single.md)

```cpp
namespace std::ranges {
  template<class T>
    requires is_object_v<T>
  class empty_view : public view_interface<empty_view<T>> {
  public:
    static constexpr T* begin() noexcept { return nullptr; }
    static constexpr T* end() noexcept { return nullptr; }
    static constexpr T* data() noexcept { return nullptr; }
    static constexpr size_t size() noexcept { return 0; }
    static constexpr bool empty() noexcept { return true; }
  };
}
```
