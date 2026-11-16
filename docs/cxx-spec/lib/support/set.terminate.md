# 17.9.4.2 `set_terminate` [set.terminate]

[Contents](../../README.md) · up: [17.9.4 Abnormal termination \[exception.terminate\]](exception.terminate.md) · prev: [17.9.4.1 Type `terminate_handler` \[terminate.handler\]](terminate.handler.md) · next: [17.9.4.3 `get_terminate` \[get.terminate\]](get.terminate.md)

```cpp
terminate_handler set_terminate(terminate_handler f) noexcept;
```

**1** *Effects:* Establishes the function designated by `f` as the current handler function for terminating exception processing.

**2** *Remarks:* It is unspecified whether a null pointer value designates the default `terminate_handler`.

**3** *Returns:* The previous `terminate_handler`.
