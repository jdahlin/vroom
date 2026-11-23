# 17.12.4.2.2 Resumption [coroutine.handle.noop.resumption]

[Contents](../../README.md) · up: [17.12.4.2 Class `coroutine_handle<noop_coroutine_promise>` \[coroutine.handle.noop\]](coroutine.handle.noop.md) · prev: [17.12.4.2.1 Observers \[coroutine.handle.noop.observers\]](coroutine.handle.noop.observers.md) · next: [17.12.4.2.3 Promise access \[coroutine.handle.noop.promise\]](coroutine.handle.noop.promise.md)

```cpp
constexpr void operator()() const noexcept;
constexpr void resume() const noexcept;
constexpr void destroy() const noexcept;
```

**1** *Effects:* None.

**2** *Remarks:* If `noop_coroutine_handle` is converted to `coroutine_handle<>`, calls to `operator()`, `resume` and `destroy` on that handle will also have no observable effects.
