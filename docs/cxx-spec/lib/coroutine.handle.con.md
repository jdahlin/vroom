# 17.12.3.1 Construct/reset [coroutine.handle.con]

[Contents](../README.md) · up: [17.12.3 Class template `coroutine_handle` \[coroutine.handle\]](coroutine.handle.md) · prev: [17.12.3 Class template `coroutine_handle` \[coroutine.handle\]](coroutine.handle.md) · next: [17.12.3.2 Export/import \[coroutine.handle.export.import\]](coroutine.handle.export.import.md)

```cpp
constexpr coroutine_handle() noexcept;
constexpr coroutine_handle(nullptr_t) noexcept;
```

**1** *Postconditions:* `address() == nullptr`.

```cpp
static coroutine_handle from_promise(Promise& p);
```

**2** *Preconditions:* `p` is a reference to a promise object of a coroutine.

**3** *Returns:* A coroutine handle `h` referring to the coroutine.

**4** *Postconditions:* `addressof(h.promise()) == addressof(p)`.

```cpp
coroutine_handle& operator=(nullptr_t) noexcept;
```

**5** *Postconditions:* `address() == nullptr`.

**6** *Returns:* `*this`.
