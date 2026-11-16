# 17.10.3 Initializer list access [support.initlist.access]

[Contents](../README.md) · up: [17.10 Initializer lists \[support.initlist\]](support.initlist.md) · prev: [17.10.2 Initializer list constructors \[support.initlist.cons\]](support.initlist.cons.md) · next: [17.10.4 Initializer list range access \[support.initlist.range\]](support.initlist.range.md)

```cpp
constexpr const E* begin() const noexcept;
```

**1** *Returns:* A pointer to the beginning of the array. If `size() == 0` the values of `begin()` and `end()` are unspecified but they shall be identical.

```cpp
constexpr const E* end() const noexcept;
```

**2** *Returns:* `begin() + size()`.

```cpp
constexpr size_t size() const noexcept;
```

**3** *Returns:* The number of elements in the array.

**4** *Complexity:* Constant time.
