# 17.10.4 Initializer list range access [support.initlist.range]

[Contents](../../README.md) · up: [17.10 Initializer lists \[support.initlist\]](support.initlist.md) · prev: [17.10.3 Initializer list access \[support.initlist.access\]](support.initlist.access.md) · next: [17.11 Comparisons \[cmp\]](cmp.md)

```cpp
template<class E> constexpr const E* begin(initializer_list<E> il) noexcept;
```

**1** *Returns:* `il.begin()`.

```cpp
template<class E> constexpr const E* end(initializer_list<E> il) noexcept;
```

**2** *Returns:* `il.end()`.
