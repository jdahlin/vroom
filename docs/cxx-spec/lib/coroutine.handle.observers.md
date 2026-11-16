# 17.12.3.3 Observers [coroutine.handle.observers]

[Contents](../README.md) · up: [17.12.3 Class template `coroutine_handle` \[coroutine.handle\]](coroutine.handle.md) · prev: [17.12.3.2 Export/import \[coroutine.handle.export.import\]](coroutine.handle.export.import.md) · next: [17.12.3.4 Resumption \[coroutine.handle.resumption\]](coroutine.handle.resumption.md)

```cpp
constexpr explicit operator bool() const noexcept;
```

**1** *Returns:* `address() != nullptr`.

```cpp
bool done() const;
```

**2** *Preconditions:* `*this` refers to a suspended coroutine.

**3** *Returns:* `true` if the coroutine is suspended at its final suspend point, otherwise `false`.
