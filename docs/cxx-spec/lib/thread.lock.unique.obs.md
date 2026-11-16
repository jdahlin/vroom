# 32.5.4.3.4 Observers [thread.lock.unique.obs]

[Contents](../README.md) · up: [32.5.4.3 Class template `unique_lock` \[thread.lock.unique\]](thread.lock.unique.md) · prev: [32.5.4.3.3 Modifiers \[thread.lock.unique.mod\]](thread.lock.unique.mod.md) · next: [32.5.4.4 Class template `shared_lock` \[thread.lock.shared\]](thread.lock.shared.md)

```cpp
bool owns_lock() const noexcept;
```

**1** *Returns:* `owns`.

```cpp
explicit operator bool() const noexcept;
```

**2** *Returns:* `owns`.

```cpp
mutex_type *mutex() const noexcept;
```

**3** *Returns:* `pm`.
