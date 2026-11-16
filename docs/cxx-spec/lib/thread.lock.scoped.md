# 32.5.4.2 Class template `scoped_lock` [thread.lock.scoped]

[Contents](../README.md) · up: [32.5.4 Locks \[thread.lock\]](thread.lock.md) · prev: [32.5.4.1 Class template `lock_guard` \[thread.lock.guard\]](thread.lock.guard.md) · next: [32.5.4.3 Class template `unique_lock` \[thread.lock.unique\]](thread.lock.unique.md)

```cpp
namespace std {
  template<class... MutexTypes>
  class scoped_lock {
  public:
    using mutex_type = Mutex;   // If MutexTypes... consists of the single type Mutex

    explicit scoped_lock(MutexTypes&... m);
    explicit scoped_lock(adopt_lock_t, MutexTypes&... m);
    ~scoped_lock();

    scoped_lock(const scoped_lock&) = delete;
    scoped_lock& operator=(const scoped_lock&) = delete;

  private:
    tuple<MutexTypes&...> pm;   // exposition only
  };
}
```

**1** An object of type `scoped_lock` controls the ownership of lockable objects within a scope. A `scoped_lock` object maintains ownership of lockable objects throughout the `scoped_lock` object’s lifetime ([[basic.life]](../basic.life.md)). The behavior of a program is undefined if the lockable objects referenced by `pm` do not exist for the entire lifetime of the `scoped_lock` object. When `sizeof...(MutexTypes)` is `1`, the supplied `Mutex` type shall meet the *Cpp17BasicLockable* requirements ([[thread.req.lockable.basic]](thread.req.lockable.basic.md)). Otherwise, each of the mutex types shall meet the *Cpp17Lockable* requirements ([[thread.req.lockable.req]](thread.req.lockable.req.md)).

```cpp
explicit scoped_lock(MutexTypes&... m);
```

**2** *Preconditions:* If a `MutexTypes` type is not a recursive mutex, the calling thread does not own the corresponding mutex element of `m`.

**3** *Effects:* Initializes `pm` with `tie(m...)`. Then if `sizeof...(MutexTypes)` is `0`, no effects. Otherwise if `sizeof...(MutexTypes)` is `1`, then `m.lock()`. Otherwise, `lock(m...)`.

```cpp
explicit scoped_lock(adopt_lock_t, MutexTypes&... m);
```

**4** *Preconditions:* The calling thread owns all the mutexes in `m`.

**5** *Effects:* Initializes `pm` with `tie(m...)`.

**6** *Throws:* Nothing.

```cpp
~scoped_lock();
```

**7** *Effects:* For all `i` in `[0, sizeof...(MutexTypes))`, `get<i>(pm).unlock()`.
