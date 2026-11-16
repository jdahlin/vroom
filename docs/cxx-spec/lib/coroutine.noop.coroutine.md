# 17.12.4.3 Function `noop_coroutine` [coroutine.noop.coroutine]

[Contents](../README.md) · up: [17.12.4 No-op coroutines \[coroutine.noop\]](coroutine.noop.md) · prev: [17.12.4.2.4 Address \[coroutine.handle.noop.address\]](coroutine.handle.noop.address.md) · next: [17.12.5 Trivial awaitables \[coroutine.trivial.awaitables\]](coroutine.trivial.awaitables.md)

```cpp
noop_coroutine_handle noop_coroutine() noexcept;
```

**1** *Returns:* A handle to a coroutine that has no observable effects when resumed or destroyed.

**2** *Remarks:* A handle returned from `noop_coroutine` may or may not compare equal to a handle returned from another invocation of `noop_coroutine`.
