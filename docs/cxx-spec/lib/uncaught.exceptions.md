# 17.9.5 `uncaught_exceptions` [uncaught.exceptions]

[Contents](../README.md) · up: [17.9 Exception handling \[support.exception\]](support.exception.md) · prev: [17.9.4.4 `terminate` \[terminate\]](terminate.md) · next: [17.9.6 Exception propagation \[propagation\]](propagation.md)

```cpp
int uncaught_exceptions() noexcept;
```

**1** *Returns:* The number of uncaught exceptions ([[except.uncaught]](../except.uncaught.md)).

**2** *Remarks:* When `uncaught_exceptions() > 0`, throwing an exception can result in a call of the function `std::terminate` ([[except.terminate]](../except.terminate.md)).
