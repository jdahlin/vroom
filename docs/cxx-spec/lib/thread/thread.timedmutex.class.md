# 32.5.3.3.1 Class `timed_mutex` [thread.timedmutex.class]

[Contents](../../README.md) · up: [32.5.3.3 Timed mutex types \[thread.timedmutex.requirements\]](thread.timedmutex.requirements.md) · prev: [32.5.3.3 Timed mutex types \[thread.timedmutex.requirements\]](thread.timedmutex.requirements.md) · next: [32.5.3.3.2 Class `recursive_timed_mutex` \[thread.timedmutex.recursive\]](thread.timedmutex.recursive.md)

```cpp
namespace std {
  class timed_mutex {
  public:
    timed_mutex();
    ~timed_mutex();

    timed_mutex(const timed_mutex&) = delete;
    timed_mutex& operator=(const timed_mutex&) = delete;

    void lock();    // blocking
    bool try_lock();
    template<class Rep, class Period>
      bool try_lock_for(const chrono::duration<Rep, Period>& rel_time);
    template<class Clock, class Duration>
      bool try_lock_until(const chrono::time_point<Clock, Duration>& abs_time);
    void unlock();

    using native_handle_type = implementation-defined;          // see [thread.req.native]
    native_handle_type native_handle();                         // see [thread.req.native]
  };
}
```

**1** The class `timed_mutex` provides a non-recursive mutex with exclusive ownership semantics. If one thread owns a `timed_mutex` object, attempts by another thread to acquire ownership of that object will fail (for `try_lock()`) or block (for `lock()`, `try_lock_for()`, and `try_lock_until()`) until the owning thread has released ownership with a call to `unlock()` or the call to `try_lock_for()` or `try_lock_until()` times out (having failed to obtain ownership).

**2** The class `timed_mutex` meets all of the timed mutex requirements ([[thread.timedmutex.requirements]](thread.timedmutex.requirements.md)). It is a standard-layout class ([[class.prop]](../../class/class.prop.md)).

**3** The behavior of a program is undefined if:

- it destroys a `timed_mutex` object owned by any thread,
- a thread that owns a `timed_mutex` object calls `lock()`, `try_lock()`, `try_lock_for()`, or `try_lock_until()` on that object, or
- a thread terminates while owning a `timed_mutex` object.
