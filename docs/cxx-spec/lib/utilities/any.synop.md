# 20.8.1 Header `<any>` synopsis [any.synop]

[Contents](../../README.md) · up: [20.8 Storage for any type \[any\]](any.md) · prev: [20.8 Storage for any type \[any\]](any.md) · next: [20.8.2 Class `bad_any_cast` \[any.bad.any.cast\]](any.bad.any.cast.md)

```cpp
namespace std {
  // [any.bad.any.cast], class bad_any_cast
  class bad_any_cast;

  // [any.class], class any
  class any;

  // [any.nonmembers], non-member functions
  void swap(any& x, any& y) noexcept;

  template<class T, class... Args>
    any make_any(Args&&... args);
  template<class T, class U, class... Args>
    any make_any(initializer_list<U> il, Args&&... args);

  template<class T>
    T any_cast(const any& operand);
  template<class T>
    T any_cast(any& operand);
  template<class T>
    T any_cast(any&& operand);

  template<class T>
    const T* any_cast(const any* operand) noexcept;
  template<class T>
    T* any_cast(any* operand) noexcept;
}
```
