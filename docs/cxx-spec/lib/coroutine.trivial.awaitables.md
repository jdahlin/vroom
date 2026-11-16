# 17.12.5 Trivial awaitables [coroutine.trivial.awaitables]

[Contents](../README.md) · up: [17.12 Coroutines \[support.coroutine\]](support.coroutine.md) · prev: [17.12.4.3 Function `noop_coroutine` \[coroutine.noop.coroutine\]](coroutine.noop.coroutine.md) · next: [17.13 Other runtime support \[support.runtime\]](support.runtime.md)

```cpp
namespace std {
  struct suspend_never {
    constexpr bool await_ready() const noexcept { return true; }
    constexpr void await_suspend(coroutine_handle<>) const noexcept {}
    constexpr void await_resume() const noexcept {}
  };
  struct suspend_always {
    constexpr bool await_ready() const noexcept { return false; }
    constexpr void await_suspend(coroutine_handle<>) const noexcept {}
    constexpr void await_resume() const noexcept {}
  };
}
```

**1** [*Note 1*: The types `suspend_never` and `suspend_always` can be used to indicate that an *await-expression* should either never suspend or always suspend, and in either case not produce a value. — *end note*]
