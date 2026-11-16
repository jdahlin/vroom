# 32.5.3.4.1 Class `shared_mutex` [thread.sharedmutex.class]

[Contents](../README.md) · up: [32.5.3.4 Shared mutex types \[thread.sharedmutex.requirements\]](thread.sharedmutex.requirements.md) · prev: [32.5.3.4 Shared mutex types \[thread.sharedmutex.requirements\]](thread.sharedmutex.requirements.md) · next: [32.5.3.5 Shared timed mutex types \[thread.sharedtimedmutex.requirements\]](thread.sharedtimedmutex.requirements.md)

```cpp
namespace std {
  class shared_mutex {
  public:
    shared_mutex();
    ~shared_mutex();

    shared_mutex(const shared_mutex&) = delete;
    shared_mutex& operator=(const shared_mutex&) = delete;

    // exclusive ownership
    void lock();                // blocking
    bool try_lock();
    void unlock();

    // shared ownership
    void lock_shared();         // blocking
    bool try_lock_shared();
    void unlock_shared();

    using native_handle_type = implementation-defined;          // see [thread.req.native]
    native_handle_type native_handle();                         // see [thread.req.native]
  };
}
```

**1** The class `shared_mutex` provides a non-recursive mutex with shared ownership semantics.

**2** The class `shared_mutex` meets all of the shared mutex requirements ([[thread.sharedmutex.requirements]](thread.sharedmutex.requirements.md)). It is a standard-layout class ([[class.prop]](../class.prop.md)).

**3** The behavior of a program is undefined if:

- it destroys a `shared_mutex` object owned by any thread,
- a thread attempts to recursively gain any ownership of a `shared_mutex`, or
- a thread terminates while possessing any ownership of a `shared_mutex`.

**4** `shared_mutex` may be a synonym for `shared_timed_mutex`.
