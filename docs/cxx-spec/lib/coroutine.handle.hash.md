# 17.12.3.7 Hash support [coroutine.handle.hash]

[Contents](../README.md) · up: [17.12.3 Class template `coroutine_handle` \[coroutine.handle\]](coroutine.handle.md) · prev: [17.12.3.6 Comparison operators \[coroutine.handle.compare\]](coroutine.handle.compare.md) · next: [17.12.4 No-op coroutines \[coroutine.noop\]](coroutine.noop.md)

```cpp
template<class P> struct hash<coroutine_handle<P>>;
```

**1** The specialization is enabled ([[unord.hash]](unord.hash.md)).
