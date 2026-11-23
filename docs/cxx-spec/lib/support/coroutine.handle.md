# 17.12.3 Class template `coroutine_handle` [coroutine.handle]

[Contents](../../README.md) · up: [17.12 Coroutines \[support.coroutine\]](support.coroutine.md) · prev: [17.12.2.1 Class template `coroutine_traits` \[coroutine.traits.primary\]](coroutine.traits.primary.md) · next: [17.12.3.1 Construct/reset \[coroutine.handle.con\]](coroutine.handle.con.md)

```cpp
namespace std {
  template<>
  struct coroutine_handle<void>
  {
    // [coroutine.handle.con], construct/reset
    constexpr coroutine_handle() noexcept;
    constexpr coroutine_handle(nullptr_t) noexcept;
    coroutine_handle& operator=(nullptr_t) noexcept;

    // [coroutine.handle.export.import], export/import
    constexpr void* address() const noexcept;
    static constexpr coroutine_handle from_address(void* addr);

    // [coroutine.handle.observers], observers
    constexpr explicit operator bool() const noexcept;
    bool done() const;

    // [coroutine.handle.resumption], resumption
    void operator()() const;
    void resume() const;
    void destroy() const;

  private:
    void* ptr;  // exposition only
  };

  template<class Promise>
  struct coroutine_handle : coroutine_handle<>
  {
    // [coroutine.handle.con], construct/reset
    using coroutine_handle<>::coroutine_handle;
    static coroutine_handle from_promise(Promise&);
    coroutine_handle& operator=(nullptr_t) noexcept;

    // [coroutine.handle.export.import], export/import
    static constexpr coroutine_handle from_address(void* addr);

    // [coroutine.handle.promise], promise access
    Promise& promise() const;
  };
}
```

**1** An object of type `coroutine_handle<T>` is called a *coroutine handle* and can be used to refer to a suspended or executing coroutine. A default-constructed `coroutine_handle` object does not refer to any coroutine.

**2** If a program declares an explicit or partial specialization of `coroutine_handle`, the behavior is undefined.

## Subclauses

- [17.12.3.1 Construct/reset \[coroutine.handle.con\]](coroutine.handle.con.md)
- [17.12.3.2 Export/import \[coroutine.handle.export.import\]](coroutine.handle.export.import.md)
- [17.12.3.3 Observers \[coroutine.handle.observers\]](coroutine.handle.observers.md)
- [17.12.3.4 Resumption \[coroutine.handle.resumption\]](coroutine.handle.resumption.md)
- [17.12.3.5 Promise access \[coroutine.handle.promise\]](coroutine.handle.promise.md)
- [17.12.3.6 Comparison operators \[coroutine.handle.compare\]](coroutine.handle.compare.md)
- [17.12.3.7 Hash support \[coroutine.handle.hash\]](coroutine.handle.hash.md)
