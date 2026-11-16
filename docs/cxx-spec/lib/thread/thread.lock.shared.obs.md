# 32.5.4.4.4 Observers [thread.lock.shared.obs]

[Contents](../../README.md) · up: [32.5.4.4 Class template `shared_lock` \[thread.lock.shared\]](thread.lock.shared.md) · prev: [32.5.4.4.3 Modifiers \[thread.lock.shared.mod\]](thread.lock.shared.mod.md) · next: [32.5.5 Generic locking algorithms \[thread.lock.algorithm\]](thread.lock.algorithm.md)

```cpp
bool owns_lock() const noexcept;
```

**1** *Returns:* `owns`.

```cpp
explicit operator bool() const noexcept;
```

**2** *Returns:* `owns`.

```cpp
mutex_type* mutex() const noexcept;
```

**3** *Returns:* `pm`.
