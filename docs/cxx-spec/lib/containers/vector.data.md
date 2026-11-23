# 22.3.11.4 Data [vector.data]

[Contents](../../README.md) · up: [22.3.11 Class template `vector` \[vector\]](vector.md) · prev: [22.3.11.3 Capacity \[vector.capacity\]](vector.capacity.md) · next: [22.3.11.5 Modifiers \[vector.modifiers\]](vector.modifiers.md)

```cpp
constexpr T*         data() noexcept;
constexpr const T*   data() const noexcept;
```

**1** *Returns:* A pointer such that `[data(), data() + size())` is a valid range. For a non-empty vector, `data()` `==` `addressof(front())`.

**2** *Complexity:* Constant time.
