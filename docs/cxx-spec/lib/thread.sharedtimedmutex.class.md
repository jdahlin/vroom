# 32.5.3.5.1 Class `shared_timed_mutex` [thread.sharedtimedmutex.class]

[Contents](../README.md) · up: [32.5.3.5 Shared timed mutex types \[thread.sharedtimedmutex.requirements\]](thread.sharedtimedmutex.requirements.md) · prev: [32.5.3.5 Shared timed mutex types \[thread.sharedtimedmutex.requirements\]](thread.sharedtimedmutex.requirements.md) · next: [32.5.4 Locks \[thread.lock\]](thread.lock.md)

```cpp
namespace std {
  class shared_timed_mutex {
  public:
    shared_timed_mutex();
    ~shared_timed_mutex();

    shared_timed_mutex(const shared_timed_mutex&) = delete;
    shared_timed_mutex& operator=(const shared_timed_mutex&) = delete;

    // exclusive ownership
    void lock();                // blocking
    bool try_lock();
    template<class Rep, class Period>
      bool try_lock_for(const chrono::duration<Rep, Period>& rel_time);
    template<class Clock, class Duration>
      bool try_lock_until(const chrono::time_point<Clock, Duration>& abs_time);
    void unlock();

    // shared ownership
    void lock_shared();         // blocking
    bool try_lock_shared();
    template<class Rep, class Period>
      bool try_lock_shared_for(const chrono::duration<Rep, Period>& rel_time);
    template<class Clock, class Duration>
      bool try_lock_shared_until(const chrono::time_point<Clock, Duration>& abs_time);
    void unlock_shared();
  };
}
```

**1** The class `shared_timed_mutex` provides a non-recursive mutex with shared ownership semantics.

**2** The class `shared_timed_mutex` meets all of the shared timed mutex requirements ([[thread.sharedtimedmutex.requirements]](thread.sharedtimedmutex.requirements.md)). It is a standard-layout class ([[class.prop]](../class.prop.md)).

**3** The behavior of a program is undefined if:

- it destroys a `shared_timed_mutex` object owned by any thread,
- a thread attempts to recursively gain any ownership of a `shared_timed_mutex`, or
- a thread terminates while possessing any ownership of a `shared_timed_mutex`.
