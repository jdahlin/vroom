# 32.5.4.3 Class template `unique_lock` [thread.lock.unique]

[Contents](../README.md) · up: [32.5.4 Locks \[thread.lock\]](thread.lock.md) · prev: [32.5.4.2 Class template `scoped_lock` \[thread.lock.scoped\]](thread.lock.scoped.md) · next: [32.5.4.3.1 Constructors, destructor, and assignment \[thread.lock.unique.cons\]](thread.lock.unique.cons.md)

```cpp
namespace std {
  template<class Mutex>
  class unique_lock {
  public:
    using mutex_type = Mutex;

    // [thread.lock.unique.cons], construct/copy/destroy
    unique_lock() noexcept;
    explicit unique_lock(mutex_type& m);
    unique_lock(mutex_type& m, defer_lock_t) noexcept;
    unique_lock(mutex_type& m, try_to_lock_t);
    unique_lock(mutex_type& m, adopt_lock_t);
    template<class Clock, class Duration>
      unique_lock(mutex_type& m, const chrono::time_point<Clock, Duration>& abs_time);
    template<class Rep, class Period>
      unique_lock(mutex_type& m, const chrono::duration<Rep, Period>& rel_time);
    ~unique_lock();

    unique_lock(const unique_lock&) = delete;
    unique_lock& operator=(const unique_lock&) = delete;

    unique_lock(unique_lock&& u) noexcept;
    unique_lock& operator=(unique_lock&& u);

    // [thread.lock.unique.locking], locking
    void lock();
    bool try_lock();

    template<class Rep, class Period>
      bool try_lock_for(const chrono::duration<Rep, Period>& rel_time);
    template<class Clock, class Duration>
      bool try_lock_until(const chrono::time_point<Clock, Duration>& abs_time);

    void unlock();

    // [thread.lock.unique.mod], modifiers
    void swap(unique_lock& u) noexcept;
    mutex_type* release() noexcept;

    // [thread.lock.unique.obs], observers
    bool owns_lock() const noexcept;
    explicit operator bool () const noexcept;
    mutex_type* mutex() const noexcept;

  private:
    mutex_type* pm;             // exposition only
    bool owns;                  // exposition only
  };

  template<class Mutex>
    void swap(unique_lock<Mutex>& x, unique_lock<Mutex>& y) noexcept;
}
```

**1** An object of type `unique_lock` controls the ownership of a lockable object within a scope. Ownership of the lockable object may be acquired at construction or after construction, and may be transferred, after acquisition, to another `unique_lock` object. Objects of type `unique_lock` are not copyable but are movable. The behavior of a program is undefined if the contained pointer `pm` is not null and the lockable object pointed to by `pm` does not exist for the entire remaining lifetime ([[basic.life]](../basic.life.md)) of the `unique_lock` object. The supplied `Mutex` type shall meet the *Cpp17BasicLockable* requirements ([[thread.req.lockable.basic]](thread.req.lockable.basic.md)).

**2** [*Note 1*: `unique_lock<Mutex>` meets the *Cpp17BasicLockable* requirements. If `Mutex` meets the *Cpp17Lockable* requirements ([[thread.req.lockable.req]](thread.req.lockable.req.md)), `unique_lock<Mutex>` also meets the *Cpp17Lockable* requirements; if `Mutex` meets the *Cpp17TimedLockable* requirements ([[thread.req.lockable.timed]](thread.req.lockable.timed.md)), `unique_lock<Mutex>` also meets the *Cpp17TimedLockable* requirements. — *end note*]

## Subclauses

- [32.5.4.3.1 Constructors, destructor, and assignment \[thread.lock.unique.cons\]](thread.lock.unique.cons.md)
- [32.5.4.3.2 Locking \[thread.lock.unique.locking\]](thread.lock.unique.locking.md)
- [32.5.4.3.3 Modifiers \[thread.lock.unique.mod\]](thread.lock.unique.mod.md)
- [32.5.4.3.4 Observers \[thread.lock.unique.obs\]](thread.lock.unique.obs.md)
