# 32.5.4.1 Class template `lock_guard` [thread.lock.guard]

[Contents](../README.md) · up: [32.5.4 Locks \[thread.lock\]](thread.lock.md) · prev: [32.5.4 Locks \[thread.lock\]](thread.lock.md) · next: [32.5.4.2 Class template `scoped_lock` \[thread.lock.scoped\]](thread.lock.scoped.md)

```cpp
namespace std {
  template<class Mutex>
  class lock_guard {
  public:
    using mutex_type = Mutex;

    explicit lock_guard(mutex_type& m);
    lock_guard(mutex_type& m, adopt_lock_t);
    ~lock_guard();

    lock_guard(const lock_guard&) = delete;
    lock_guard& operator=(const lock_guard&) = delete;

  private:
    mutex_type& pm;             // exposition only
  };
}
```

**1** An object of type `lock_guard` controls the ownership of a lockable object within a scope. A `lock_guard` object maintains ownership of a lockable object throughout the `lock_guard` object’s lifetime ([[basic.life]](../basic.life.md)). The behavior of a program is undefined if the lockable object referenced by `pm` does not exist for the entire lifetime of the `lock_guard` object. The supplied `Mutex` type shall meet the *Cpp17BasicLockable* requirements ([[thread.req.lockable.basic]](thread.req.lockable.basic.md)).

```cpp
explicit lock_guard(mutex_type& m);
```

**2** *Preconditions:* If `mutex_type` is not a recursive mutex, the calling thread does not own the mutex `m`.

**3** *Effects:* Initializes `pm` with `m`. Calls `m.lock()`.

```cpp
lock_guard(mutex_type& m, adopt_lock_t);
```

**4** *Preconditions:* The calling thread owns the mutex `m`.

**5** *Effects:* Initializes `pm` with `m`.

**6** *Throws:* Nothing.

```cpp
~lock_guard();
```

**7** *Effects:* As if by `pm.unlock()`.
