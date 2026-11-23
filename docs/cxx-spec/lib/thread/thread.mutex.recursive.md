# 32.5.3.2.2 Class `recursive_mutex` [thread.mutex.recursive]

[Contents](../../README.md) · up: [32.5.3.2 Mutex types \[thread.mutex.requirements.mutex\]](thread.mutex.requirements.mutex.md) · prev: [32.5.3.2.1 Class `mutex` \[thread.mutex.class\]](thread.mutex.class.md) · next: [32.5.3.3 Timed mutex types \[thread.timedmutex.requirements\]](thread.timedmutex.requirements.md)

```cpp
namespace std {
  class recursive_mutex {
  public:
    recursive_mutex();
    ~recursive_mutex();

    recursive_mutex(const recursive_mutex&) = delete;
    recursive_mutex& operator=(const recursive_mutex&) = delete;

    void lock();
    bool try_lock() noexcept;
    void unlock();

    using native_handle_type = implementation-defined;          // see [thread.req.native]
    native_handle_type native_handle();                         // see [thread.req.native]
  };
}
```

**1** The class `recursive_mutex` provides a recursive mutex with exclusive ownership semantics. If one thread owns a `recursive_mutex` object, attempts by another thread to acquire ownership of that object will fail (for `try_lock()`) or block (for `lock()`) until the first thread has completely released ownership.

**2** The class `recursive_mutex` meets all of the mutex requirements ([[thread.mutex.requirements]](thread.mutex.requirements.md)). It is a standard-layout class ([[class.prop]](../../class/class.prop.md)).

**3** A thread that owns a `recursive_mutex` object may acquire additional levels of ownership by calling `lock()` or `try_lock()` on that object. It is unspecified how many levels of ownership may be acquired by a single thread. If a thread has already acquired the maximum level of ownership for a `recursive_mutex` object, additional calls to `try_lock()` fail, and additional calls to `lock()` throw an exception of type `system_error`. A thread shall call `unlock()` once for each level of ownership acquired by calls to `lock()` and `try_lock()`. Only when all levels of ownership have been released may ownership be acquired by another thread.

**4** The behavior of a program is undefined if:

- it destroys a `recursive_mutex` object owned by any thread or
- a thread terminates while owning a `recursive_mutex` object.
