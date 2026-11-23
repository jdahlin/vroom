# 20.14.16.2.5 Target access [func.wrap.func.targ]

[Contents](../../README.md) · up: [20.14.16.2 Class template `function` \[func.wrap.func\]](func.wrap.func.md) · prev: [20.14.16.2.4 Invocation \[func.wrap.func.inv\]](func.wrap.func.inv.md) · next: [20.14.16.2.6 Null pointer comparison functions \[func.wrap.func.nullptr\]](func.wrap.func.nullptr.md)

```cpp
const type_info& target_type() const noexcept;
```

**1** *Returns:* If `*this` has a target of type `T`, `typeid(T)`; otherwise, `typeid(void)`.

```cpp
template<class T>       T* target() noexcept;
template<class T> const T* target() const noexcept;
```

**2** *Returns:* If `target_type() == typeid(T)` a pointer to the stored function target; otherwise a null pointer.
