# 32.5.3.2.1 Class `mutex` [thread.mutex.class]

[Contents](../../README.md) · up: [32.5.3.2 Mutex types \[thread.mutex.requirements.mutex\]](thread.mutex.requirements.mutex.md) · prev: [32.5.3.2 Mutex types \[thread.mutex.requirements.mutex\]](thread.mutex.requirements.mutex.md) · next: [32.5.3.2.2 Class `recursive_mutex` \[thread.mutex.recursive\]](thread.mutex.recursive.md)

```cpp
namespace std {
  class mutex {
  public:
    constexpr mutex() noexcept;
    ~mutex();

    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;

    void lock();
    bool try_lock();
    void unlock();

    using native_handle_type = implementation-defined;          // see [thread.req.native]
    native_handle_type native_handle();                         // see [thread.req.native]
  };
}
```

**1** The class `mutex` provides a non-recursive mutex with exclusive ownership semantics. If one thread owns a mutex object, attempts by another thread to acquire ownership of that object will fail (for `try_lock()`) or block (for `lock()`) until the owning thread has released ownership with a call to `unlock()`.

**2** [*Note 1*: After a thread `A` has called `unlock()`, releasing a mutex, it is possible for another thread `B` to lock the same mutex, observe that it is no longer in use, unlock it, and destroy it, before thread `A` appears to have returned from its unlock call. Implementations are required to handle such scenarios correctly, as long as thread `A` doesn’t access the mutex after the unlock call returns. These cases typically occur when a reference-counted object contains a mutex that is used to protect the reference count. — *end note*]

**3** The class `mutex` meets all of the mutex requirements ([[thread.mutex.requirements]](thread.mutex.requirements.md)). It is a standard-layout class ([[class.prop]](../../class/class.prop.md)).

**4** [*Note 2*: A program can deadlock if the thread that owns a `mutex` object calls `lock()` on that object. If the implementation can detect the deadlock, a `resource_deadlock_would_occur` error condition might be observed. — *end note*]

**5** The behavior of a program is undefined if it destroys a `mutex` object owned by any thread or a thread terminates while owning a `mutex` object.
