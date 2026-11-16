# 24.6.2.2 Class template `single_view` [range.single.view]

[Contents](../../README.md) · up: [24.6.2 Single view \[range.single\]](range.single.md) · prev: [24.6.2.1 Overview \[range.single.overview\]](range.single.overview.md) · next: [24.6.3 Iota view \[range.iota\]](range.iota.md)

```cpp
namespace std::ranges {
  template<copy_constructible T>
    requires is_object_v<T>
  class single_view : public view_interface<single_view<T>> {
  private:
    semiregular-box<T> value_;      // exposition only (see [range.semi.wrap])
  public:
    single_view() = default;
    constexpr explicit single_view(const T& t);
    constexpr explicit single_view(T&& t);
    template<class... Args>
      requires constructible_from<T, Args...>
    constexpr single_view(in_place_t, Args&&... args);

    constexpr T* begin() noexcept;
    constexpr const T* begin() const noexcept;
    constexpr T* end() noexcept;
    constexpr const T* end() const noexcept;
    static constexpr size_t size() noexcept;
    constexpr T* data() noexcept;
    constexpr const T* data() const noexcept;
  };
}
```

```cpp
constexpr explicit single_view(const T& t);
```

**1** *Effects:* Initializes `value_` with `t`.

```cpp
constexpr explicit single_view(T&& t);
```

**2** *Effects:* Initializes `value_` with `std::move(t)`.

```cpp
template<class... Args>
constexpr single_view(in_place_t, Args&&... args);
```

**3** *Effects:* Initializes `value_` as if by `value_{in_place, std::forward<Args>(args)...}`.

```cpp
constexpr T* begin() noexcept;
constexpr const T* begin() const noexcept;
```

**4** *Effects:* Equivalent to: `return data();`

```cpp
constexpr T* end() noexcept;
constexpr const T* end() const noexcept;
```

**5** *Effects:* Equivalent to: `return data() + 1;`

```cpp
static constexpr size_t size() noexcept;
```

**6** *Effects:* Equivalent to: `return 1;`

```cpp
constexpr T* data() noexcept;
constexpr const T* data() const noexcept;
```

**7** *Effects:* Equivalent to: `return value_.operator->();`
