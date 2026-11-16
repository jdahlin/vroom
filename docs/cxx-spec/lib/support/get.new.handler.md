# 17.6.3.5 `get_new_handler` [get.new.handler]

[Contents](../../README.md) · up: [17.6.3 Storage allocation errors \[alloc.errors\]](alloc.errors.md) · prev: [17.6.3.4 `set_new_handler` \[set.new.handler\]](set.new.handler.md) · next: [17.6.4 Pointer optimization barrier \[ptr.launder\]](ptr.launder.md)

```cpp
new_handler get_new_handler() noexcept;
```

**1** *Returns:* The current `new_handler`. [*Note 1*: This may be a null pointer value. — *end note*]
