# 32.5.3.3.2 Class `recursive_timed_mutex` [thread.timedmutex.recursive]

[Contents](../../README.md) · up: [32.5.3.3 Timed mutex types \[thread.timedmutex.requirements\]](thread.timedmutex.requirements.md) · prev: [32.5.3.3.1 Class `timed_mutex` \[thread.timedmutex.class\]](thread.timedmutex.class.md) · next: [32.5.3.4 Shared mutex types \[thread.sharedmutex.requirements\]](thread.sharedmutex.requirements.md)

```cpp
namespace std {
  class recursive_timed_mutex {
  public:
    recursive_timed_mutex();
    ~recursive_timed_mutex();

    recursive_timed_mutex(const recursive_timed_mutex&) = delete;
    recursive_timed_mutex& operator=(const recursive_timed_mutex&) = delete;

    void lock();    // blocking
    bool try_lock() noexcept;
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

**1** The class `recursive_timed_mutex` provides a recursive mutex with exclusive ownership semantics. If one thread owns a `recursive_timed_mutex` object, attempts by another thread to acquire ownership of that object will fail (for `try_lock()`) or block (for `lock()`, `try_lock_for()`, and `try_lock_until()`) until the owning thread has completely released ownership or the call to `try_lock_for()` or `try_lock_until()` times out (having failed to obtain ownership).

**2** The class `recursive_timed_mutex` meets all of the timed mutex requirements ([[thread.timedmutex.requirements]](thread.timedmutex.requirements.md)). It is a standard-layout class ([[class.prop]](../../class/class.prop.md)).

**3** A thread that owns a `recursive_timed_mutex` object may acquire additional levels of ownership by calling `lock()`, `try_lock()`, `try_lock_for()`, or `try_lock_until()` on that object. It is unspecified how many levels of ownership may be acquired by a single thread. If a thread has already acquired the maximum level of ownership for a `recursive_timed_mutex` object, additional calls to `try_lock()`, `try_lock_for()`, or `try_lock_until()` fail, and additional calls to `lock()` throw an exception of type `system_error`. A thread shall call `unlock()` once for each level of ownership acquired by calls to `lock()`, `try_lock()`, `try_lock_for()`, and `try_lock_until()`. Only when all levels of ownership have been released may ownership of the object be acquired by another thread.

**4** The behavior of a program is undefined if:

- it destroys a `recursive_timed_mutex` object owned by any thread, or
- a thread terminates while owning a `recursive_timed_mutex` object.
