# 32.5.4.4.3 Modifiers [thread.lock.shared.mod]

[Contents](../README.md) · up: [32.5.4.4 Class template `shared_lock` \[thread.lock.shared\]](thread.lock.shared.md) · prev: [32.5.4.4.2 Locking \[thread.lock.shared.locking\]](thread.lock.shared.locking.md) · next: [32.5.4.4.4 Observers \[thread.lock.shared.obs\]](thread.lock.shared.obs.md)

```cpp
void swap(shared_lock& sl) noexcept;
```

**1** *Effects:* Swaps the data members of `*this` and `sl`.

```cpp
mutex_type* release() noexcept;
```

**2** *Returns:* The previous value of `pm`.

**3** *Postconditions:* `pm == nullptr` and `owns == false`.

```cpp
template<class Mutex>
  void swap(shared_lock<Mutex>& x, shared_lock<Mutex>& y) noexcept;
```

**4** *Effects:* As if by `x.swap(y)`.
