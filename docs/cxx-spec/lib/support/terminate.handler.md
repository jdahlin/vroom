# 17.9.4.1 Type `terminate_handler` [terminate.handler]

[Contents](../../README.md) · up: [17.9.4 Abnormal termination \[exception.terminate\]](exception.terminate.md) · prev: [17.9.4 Abnormal termination \[exception.terminate\]](exception.terminate.md) · next: [17.9.4.2 `set_terminate` \[set.terminate\]](set.terminate.md)

```cpp
using terminate_handler = void (*)();
```

**1** The type of a *handler function* to be called by `std::terminate()` when terminating exception processing.

**2** *Required behavior:* A `terminate_handler` shall terminate execution of the program without returning to the caller.

**3** *Default behavior:* The implementation’s default `terminate_handler` calls `abort()`.
