# 17.6.3.3 Type `new_handler` [new.handler]

[Contents](../README.md) · up: [17.6.3 Storage allocation errors \[alloc.errors\]](alloc.errors.md) · prev: [17.6.3.2 Class `bad_array_new_length` \[new.badlength\]](new.badlength.md) · next: [17.6.3.4 `set_new_handler` \[set.new.handler\]](set.new.handler.md)

```cpp
using new_handler = void (*)();
```

**1** The type of a *handler function* to be called by `operator new()` or `operator new[]()` ([[new.delete]](new.delete.md)) when they cannot satisfy a request for additional storage.

**2** *Required behavior:* A `new_handler` shall perform one of the following:

- make more storage available for allocation and then return;
- throw an exception of type `bad_alloc` or a class derived from `bad_alloc`;
- terminate execution of the program without returning to the caller.
