# 32.5.4 Locks [thread.lock]

[Contents](../README.md) · up: [32.5 Mutual exclusion \[thread.mutex\]](thread.mutex.md) · prev: [32.5.3.5.1 Class `shared_timed_mutex` \[thread.sharedtimedmutex.class\]](thread.sharedtimedmutex.class.md) · next: [32.5.4.1 Class template `lock_guard` \[thread.lock.guard\]](thread.lock.guard.md)

**1** A *lock* is an object that holds a reference to a lockable object and may unlock the lockable object during the lock’s destruction (such as when leaving block scope). An execution agent may use a lock to aid in managing ownership of a lockable object in an exception safe manner. A lock is said to *own* a lockable object if it is currently managing the ownership of that lockable object for an execution agent. A lock does not manage the lifetime of the lockable object it references. [*Note 1*: Locks are intended to ease the burden of unlocking the lockable object under both normal and exceptional circumstances. — *end note*]

**2** Some lock constructors take tag types which describe what should be done with the lockable object during the lock’s construction.

```cpp
namespace std {
  struct defer_lock_t  { };     // do not acquire ownership of the mutex
  struct try_to_lock_t { };     // try to acquire ownership of the mutex
                                // without blocking
  struct adopt_lock_t  { };     // assume the calling thread has already
                                // obtained mutex ownership and manage it

  inline constexpr defer_lock_t   defer_lock { };
  inline constexpr try_to_lock_t  try_to_lock { };
  inline constexpr adopt_lock_t   adopt_lock { };
}
```

## Subclauses

- [32.5.4.1 Class template `lock_guard` \[thread.lock.guard\]](thread.lock.guard.md)
- [32.5.4.2 Class template `scoped_lock` \[thread.lock.scoped\]](thread.lock.scoped.md)
- [32.5.4.3 Class template `unique_lock` \[thread.lock.unique\]](thread.lock.unique.md)
  - [32.5.4.3.1 Constructors, destructor, and assignment \[thread.lock.unique.cons\]](thread.lock.unique.cons.md)
  - [32.5.4.3.2 Locking \[thread.lock.unique.locking\]](thread.lock.unique.locking.md)
  - [32.5.4.3.3 Modifiers \[thread.lock.unique.mod\]](thread.lock.unique.mod.md)
  - [32.5.4.3.4 Observers \[thread.lock.unique.obs\]](thread.lock.unique.obs.md)
- [32.5.4.4 Class template `shared_lock` \[thread.lock.shared\]](thread.lock.shared.md)
  - [32.5.4.4.1 Constructors, destructor, and assignment \[thread.lock.shared.cons\]](thread.lock.shared.cons.md)
  - [32.5.4.4.2 Locking \[thread.lock.shared.locking\]](thread.lock.shared.locking.md)
  - [32.5.4.4.3 Modifiers \[thread.lock.shared.mod\]](thread.lock.shared.mod.md)
  - [32.5.4.4.4 Observers \[thread.lock.shared.obs\]](thread.lock.shared.obs.md)
