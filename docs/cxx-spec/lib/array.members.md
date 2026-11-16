# 22.3.7.3 Member functions [array.members]

[Contents](../README.md) · up: [22.3.7 Class template `array` \[array\]](array.md) · prev: [22.3.7.2 Constructors, copy, and assignment \[array.cons\]](array.cons.md) · next: [22.3.7.4 Specialized algorithms \[array.special\]](array.special.md)

```cpp
constexpr size_type size() const noexcept;
```

**1** *Returns:* `N`.

```cpp
constexpr T* data() noexcept;
constexpr const T* data() const noexcept;
```

**2** *Returns:* A pointer such that `[data(), data() + size())` is a valid range. For a non-empty array, `data()` `==` `addressof(front())`.

```cpp
constexpr void fill(const T& u);
```

**3** *Effects:* As if by `fill_n(begin(), N, u)`.

```cpp
constexpr void swap(array& y) noexcept(is_nothrow_swappable_v<T>);
```

**4** *Effects:* Equivalent to `swap_ranges(begin(), end(), y.begin())`.

**5** [*Note 1*: Unlike the `swap` function for other containers, `array::swap` takes linear time, may exit via an exception, and does not cause iterators to become associated with the other container. — *end note*]
