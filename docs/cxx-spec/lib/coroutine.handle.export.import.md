# 17.12.3.2 Export/import [coroutine.handle.export.import]

[Contents](../README.md) · up: [17.12.3 Class template `coroutine_handle` \[coroutine.handle\]](coroutine.handle.md) · prev: [17.12.3.1 Construct/reset \[coroutine.handle.con\]](coroutine.handle.con.md) · next: [17.12.3.3 Observers \[coroutine.handle.observers\]](coroutine.handle.observers.md)

```cpp
constexpr void* address() const noexcept;
```

**1** *Returns:* `ptr`.

```cpp
static constexpr coroutine_handle<> coroutine_handle<>::from_address(void* addr);
static constexpr coroutine_handle<Promise> coroutine_handle<Promise>::from_address(void* addr);
```

**2** *Preconditions:* `addr` was obtained via a prior call to `address`.

**3** *Postconditions:* `from_address(address()) == *this`.
