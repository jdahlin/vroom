# 32.5.4.4 Class template `shared_lock` [thread.lock.shared]

[Contents](../../README.md) · up: [32.5.4 Locks \[thread.lock\]](thread.lock.md) · prev: [32.5.4.3.4 Observers \[thread.lock.unique.obs\]](thread.lock.unique.obs.md) · next: [32.5.4.4.1 Constructors, destructor, and assignment \[thread.lock.shared.cons\]](thread.lock.shared.cons.md)

```cpp
namespace std {
  template<class Mutex>
  class shared_lock {
  public:
    using mutex_type = Mutex;

    // [thread.lock.shared.cons], construct/copy/destroy
    shared_lock() noexcept;
    explicit shared_lock(mutex_type& m);        // blocking
    shared_lock(mutex_type& m, defer_lock_t) noexcept;
    shared_lock(mutex_type& m, try_to_lock_t);
    shared_lock(mutex_type& m, adopt_lock_t);
    template<class Clock, class Duration>
      shared_lock(mutex_type& m, const chrono::time_point<Clock, Duration>& abs_time);
    template<class Rep, class Period>
      shared_lock(mutex_type& m, const chrono::duration<Rep, Period>& rel_time);
    ~shared_lock();

    shared_lock(const shared_lock&) = delete;
    shared_lock& operator=(const shared_lock&) = delete;

    shared_lock(shared_lock&& u) noexcept;
    shared_lock& operator=(shared_lock&& u) noexcept;

    // [thread.lock.shared.locking], locking
    void lock();                                // blocking
    bool try_lock();
    template<class Rep, class Period>
      bool try_lock_for(const chrono::duration<Rep, Period>& rel_time);
    template<class Clock, class Duration>
      bool try_lock_until(const chrono::time_point<Clock, Duration>& abs_time);
    void unlock();

    // [thread.lock.shared.mod], modifiers
    void swap(shared_lock& u) noexcept;
    mutex_type* release() noexcept;

    // [thread.lock.shared.obs], observers
    bool owns_lock() const noexcept;
    explicit operator bool () const noexcept;
    mutex_type* mutex() const noexcept;

  private:
    mutex_type* pm;                             // exposition only
    bool owns;                                  // exposition only
  };

  template<class Mutex>
    void swap(shared_lock<Mutex>& x, shared_lock<Mutex>& y) noexcept;
}
```

**1** An object of type `shared_lock` controls the shared ownership of a lockable object within a scope. Shared ownership of the lockable object may be acquired at construction or after construction, and may be transferred, after acquisition, to another `shared_lock` object. Objects of type `shared_lock` are not copyable but are movable. The behavior of a program is undefined if the contained pointer `pm` is not null and the lockable object pointed to by `pm` does not exist for the entire remaining lifetime ([[basic.life]](../../basic/basic.life.md)) of the `shared_lock` object. The supplied `Mutex` type shall meet the shared mutex requirements ([[thread.sharedtimedmutex.requirements]](thread.sharedtimedmutex.requirements.md)).

**2** [*Note 1*: `shared_lock<Mutex>` meets the *Cpp17TimedLockable* requirements ([[thread.req.lockable.timed]](thread.req.lockable.timed.md)). — *end note*]

## Subclauses

- [32.5.4.4.1 Constructors, destructor, and assignment \[thread.lock.shared.cons\]](thread.lock.shared.cons.md)
- [32.5.4.4.2 Locking \[thread.lock.shared.locking\]](thread.lock.shared.locking.md)
- [32.5.4.4.3 Modifiers \[thread.lock.shared.mod\]](thread.lock.shared.mod.md)
- [32.5.4.4.4 Observers \[thread.lock.shared.obs\]](thread.lock.shared.obs.md)
