# 17.12.3.6 Comparison operators [coroutine.handle.compare]

[Contents](../../README.md) · up: [17.12.3 Class template `coroutine_handle` \[coroutine.handle\]](coroutine.handle.md) · prev: [17.12.3.5 Promise access \[coroutine.handle.promise\]](coroutine.handle.promise.md) · next: [17.12.3.7 Hash support \[coroutine.handle.hash\]](coroutine.handle.hash.md)

```cpp
constexpr bool operator==(coroutine_handle<> x, coroutine_handle<> y) noexcept;
```

**1** *Returns:* `x.address() == y.address()`.

```cpp
constexpr strong_ordering operator<=>(coroutine_handle<> x, coroutine_handle<> y) noexcept;
```

**2** *Returns:* `compare_three_way()(x.address(), y.address())`.
