# 17.12.4.2.4 Address [coroutine.handle.noop.address]

[Contents](../../README.md) · up: [17.12.4.2 Class `coroutine_handle<noop_coroutine_promise>` \[coroutine.handle.noop\]](coroutine.handle.noop.md) · prev: [17.12.4.2.3 Promise access \[coroutine.handle.noop.promise\]](coroutine.handle.noop.promise.md) · next: [17.12.4.3 Function `noop_coroutine` \[coroutine.noop.coroutine\]](coroutine.noop.coroutine.md)

```cpp
constexpr void* address() const noexcept;
```

**1** *Returns:* `ptr`.

**2** *Remarks:* A `noop_coroutine_handle`’s `ptr` is always a non-null pointer value.
