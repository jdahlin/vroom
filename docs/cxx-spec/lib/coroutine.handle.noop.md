# 17.12.4.2 Class `coroutine_handle<noop_coroutine_promise>` [coroutine.handle.noop]

[Contents](../README.md) · up: [17.12.4 No-op coroutines \[coroutine.noop\]](coroutine.noop.md) · prev: [17.12.4.1 Class `noop_coroutine_promise` \[coroutine.promise.noop\]](coroutine.promise.noop.md) · next: [17.12.4.2.1 Observers \[coroutine.handle.noop.observers\]](coroutine.handle.noop.observers.md)

```cpp
namespace std {
  template<>
  struct coroutine_handle<noop_coroutine_promise> : coroutine_handle<>
  {
    // [coroutine.handle.noop.observers], observers
    constexpr explicit operator bool() const noexcept;
    constexpr bool done() const noexcept;

    // [coroutine.handle.noop.resumption], resumption
    constexpr void operator()() const noexcept;
    constexpr void resume() const noexcept;
    constexpr void destroy() const noexcept;

    // [coroutine.handle.noop.promise], promise access
    noop_coroutine_promise& promise() const noexcept;

    // [coroutine.handle.noop.address], address
    constexpr void* address() const noexcept;
  private:
    coroutine_handle(unspecified);
  };
}
```

## Subclauses

- [17.12.4.2.1 Observers \[coroutine.handle.noop.observers\]](coroutine.handle.noop.observers.md)
- [17.12.4.2.2 Resumption \[coroutine.handle.noop.resumption\]](coroutine.handle.noop.resumption.md)
- [17.12.4.2.3 Promise access \[coroutine.handle.noop.promise\]](coroutine.handle.noop.promise.md)
- [17.12.4.2.4 Address \[coroutine.handle.noop.address\]](coroutine.handle.noop.address.md)
