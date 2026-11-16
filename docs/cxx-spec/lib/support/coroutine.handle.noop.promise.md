# 17.12.4.2.3 Promise access [coroutine.handle.noop.promise]

[Contents](../../README.md) · up: [17.12.4.2 Class `coroutine_handle<noop_coroutine_promise>` \[coroutine.handle.noop\]](coroutine.handle.noop.md) · prev: [17.12.4.2.2 Resumption \[coroutine.handle.noop.resumption\]](coroutine.handle.noop.resumption.md) · next: [17.12.4.2.4 Address \[coroutine.handle.noop.address\]](coroutine.handle.noop.address.md)

```cpp
noop_coroutine_promise& promise() const noexcept;
```

**1** *Returns:* A reference to the promise object associated with this coroutine handle.
