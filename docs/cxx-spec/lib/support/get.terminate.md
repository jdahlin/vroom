# 17.9.4.3 `get_terminate` [get.terminate]

[Contents](../../README.md) · up: [17.9.4 Abnormal termination \[exception.terminate\]](exception.terminate.md) · prev: [17.9.4.2 `set_terminate` \[set.terminate\]](set.terminate.md) · next: [17.9.4.4 `terminate` \[terminate\]](terminate.md)

```cpp
terminate_handler get_terminate() noexcept;
```

**1** *Returns:* The current `terminate_handler`. [*Note 1*: This may be a null pointer value. — *end note*]
