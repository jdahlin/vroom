# 17.6.3.4 `set_new_handler` [set.new.handler]

[Contents](../../README.md) · up: [17.6.3 Storage allocation errors \[alloc.errors\]](alloc.errors.md) · prev: [17.6.3.3 Type `new_handler` \[new.handler\]](new.handler.md) · next: [17.6.3.5 `get_new_handler` \[get.new.handler\]](get.new.handler.md)

```cpp
new_handler set_new_handler(new_handler new_p) noexcept;
```

**1** *Effects:* Establishes the function designated by `new_p` as the current `new_handler`.

**2** *Returns:* The previous `new_handler`.

**3** *Remarks:* The initial `new_handler` is a null pointer.
