# 17.9.4.4 `terminate` [terminate]

[Contents](../../README.md) · up: [17.9.4 Abnormal termination \[exception.terminate\]](exception.terminate.md) · prev: [17.9.4.3 `get_terminate` \[get.terminate\]](get.terminate.md) · next: [17.9.5 `uncaught_exceptions` \[uncaught.exceptions\]](uncaught.exceptions.md)

```cpp
[[noreturn]] void terminate() noexcept;
```

**1** *Remarks:* Called by the implementation when exception handling must be abandoned for any of several reasons ([[except.terminate]](../../except/except.terminate.md)). May also be called directly by the program.

**2** *Effects:* Calls a `terminate_handler` function. It is unspecified which `terminate_handler` function will be called if an exception is active during a call to `set_terminate`. Otherwise calls the current `terminate_handler` function. [*Note 1*: A default `terminate_handler` is always considered a callable handler in this context. — *end note*]
