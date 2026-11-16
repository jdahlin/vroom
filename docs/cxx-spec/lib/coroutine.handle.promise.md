# 17.12.3.5 Promise access [coroutine.handle.promise]

[Contents](../README.md) · up: [17.12.3 Class template `coroutine_handle` \[coroutine.handle\]](coroutine.handle.md) · prev: [17.12.3.4 Resumption \[coroutine.handle.resumption\]](coroutine.handle.resumption.md) · next: [17.12.3.6 Comparison operators \[coroutine.handle.compare\]](coroutine.handle.compare.md)

```cpp
Promise& promise() const;
```

**1** *Preconditions:* `*this` refers to a coroutine.

**2** *Returns:* A reference to the promise of the coroutine.
