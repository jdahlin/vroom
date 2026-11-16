# 32.5.4.3.3 Modifiers [thread.lock.unique.mod]

[Contents](../README.md) · up: [32.5.4.3 Class template `unique_lock` \[thread.lock.unique\]](thread.lock.unique.md) · prev: [32.5.4.3.2 Locking \[thread.lock.unique.locking\]](thread.lock.unique.locking.md) · next: [32.5.4.3.4 Observers \[thread.lock.unique.obs\]](thread.lock.unique.obs.md)

```cpp
void swap(unique_lock& u) noexcept;
```

**1** *Effects:* Swaps the data members of `*this` and `u`.

```cpp
mutex_type* release() noexcept;
```

**2** *Returns:* The previous value of `pm`.

**3** *Postconditions:* `pm == 0` and `owns == false`.

```cpp
template<class Mutex>
  void swap(unique_lock<Mutex>& x, unique_lock<Mutex>& y) noexcept;
```

**4** *Effects:* As if by `x.swap(y)`.
