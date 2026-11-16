# 32.6.3 Class `condition_variable` [thread.condition.condvar]

[Contents](../README.md) · up: [32.6 Condition variables \[thread.condition\]](thread.condition.md) · prev: [32.6.2 Non-member functions \[thread.condition.nonmember\]](thread.condition.nonmember.md) · next: [32.6.4 Class `condition_variable_any` \[thread.condition.condvarany\]](thread.condition.condvarany.md)

```cpp
namespace std {
  class condition_variable {
  public:
    condition_variable();
    ~condition_variable();

    condition_variable(const condition_variable&) = delete;
    condition_variable& operator=(const condition_variable&) = delete;

    void notify_one() noexcept;
    void notify_all() noexcept;
    void wait(unique_lock<mutex>& lock);
    template<class Predicate>
      void wait(unique_lock<mutex>& lock, Predicate pred);
    template<class Clock, class Duration>
      cv_status wait_until(unique_lock<mutex>& lock,
                           const chrono::time_point<Clock, Duration>& abs_time);
    template<class Clock, class Duration, class Predicate>
      bool wait_until(unique_lock<mutex>& lock,
                      const chrono::time_point<Clock, Duration>& abs_time,
                      Predicate pred);
    template<class Rep, class Period>
      cv_status wait_for(unique_lock<mutex>& lock,
                         const chrono::duration<Rep, Period>& rel_time);
    template<class Rep, class Period, class Predicate>
      bool wait_for(unique_lock<mutex>& lock,
                    const chrono::duration<Rep, Period>& rel_time,
                    Predicate pred);

    using native_handle_type = implementation-defined;          // see [thread.req.native]
    native_handle_type native_handle();                         // see [thread.req.native]
  };
}
```

**1** The class `condition_variable` is a standard-layout class ([[class.prop]](../class.prop.md)).

```cpp
condition_variable();
```

**2** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**3** *Error conditions:*

- `resource_unavailable_try_again` — if some non-memory resource limitation prevents initialization.

```cpp
~condition_variable();
```

**4** *Preconditions:* There is no thread blocked on `*this`. [*Note 1*: That is, all threads have been notified; they could subsequently block on the lock specified in the wait. This relaxes the usual rules, which would have required all wait calls to happen before destruction. Only the notification to unblock the wait needs to happen before destruction. The user should take care to ensure that no threads wait on `*this` once the destructor has been started, especially when the waiting threads are calling the wait functions in a loop or using the overloads of `wait`, `wait_for`, or `wait_until` that take a predicate. — *end note*]

```cpp
void notify_one() noexcept;
```

**5** *Effects:* If any threads are blocked waiting for `*this`, unblocks one of those threads.

```cpp
void notify_all() noexcept;
```

**6** *Effects:* Unblocks all threads that are blocked waiting for `*this`.

```cpp
void wait(unique_lock<mutex>& lock);
```

**7** *Preconditions:* `lock.owns_lock()` is `true` and `lock.mutex()` is locked by the calling thread, and either

- no other thread is waiting on this `condition_variable` object or
- `lock.mutex()` returns the same value for each of the `lock` arguments supplied by all concurrently waiting (via `wait`, `wait_for`, or `wait_until`) threads.

**8** *Effects:*

- Atomically calls `lock.unlock()` and blocks on `*this`.
- When unblocked, calls `lock.lock()` (possibly blocking on the lock), then returns.
- The function will unblock when signaled by a call to `notify_one()` or a call to `notify_all()`, or spuriously.

**9** *Remarks:* If the function fails to meet the postcondition, `terminate()` is called ([[except.terminate]](../except.terminate.md)). [*Note 2*: This can happen if the re-locking of the mutex throws an exception. — *end note*]

**10** *Postconditions:* `lock.owns_lock()` is `true` and `lock.mutex()` is locked by the calling thread.

**11** *Throws:* Nothing.

```cpp
template<class Predicate>
  void wait(unique_lock<mutex>& lock, Predicate pred);
```

**12** *Preconditions:* `lock.owns_lock()` is `true` and `lock.mutex()` is locked by the calling thread, and either

- no other thread is waiting on this `condition_variable` object or
- `lock.mutex()` returns the same value for each of the `lock` arguments supplied by all concurrently waiting (via `wait`, `wait_for`, or `wait_until`) threads.

**13** *Effects:* Equivalent to:

```cpp
while (!pred())
  wait(lock);
```

**14** *Remarks:* If the function fails to meet the postcondition, `terminate()` is called ([[except.terminate]](../except.terminate.md)). [*Note 3*: This can happen if the re-locking of the mutex throws an exception. — *end note*]

**15** *Postconditions:* `lock.owns_lock()` is `true` and `lock.mutex()` is locked by the calling thread.

**16** *Throws:* Any exception thrown by `pred`.

```cpp
template<class Clock, class Duration>
  cv_status wait_until(unique_lock<mutex>& lock,
                       const chrono::time_point<Clock, Duration>& abs_time);
```

**17** *Preconditions:* `lock.owns_lock()` is `true` and `lock.mutex()` is locked by the calling thread, and either

- no other thread is waiting on this `condition_variable` object or
- `lock.mutex()` returns the same value for each of the `lock` arguments supplied by all concurrently waiting (via `wait`, `wait_for`, or `wait_until`) threads.

**18** *Effects:*

- Atomically calls `lock.unlock()` and blocks on `*this`.
- When unblocked, calls `lock.lock()` (possibly blocking on the lock), then returns.
- The function will unblock when signaled by a call to `notify_one()`, a call to `notify_all()`, expiration of the absolute timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `abs_time`, or spuriously.
- If the function exits via an exception, `lock.lock()` is called prior to exiting the function.

**19** *Remarks:* If the function fails to meet the postcondition, `terminate()` is called ([[except.terminate]](../except.terminate.md)). [*Note 4*: This can happen if the re-locking of the mutex throws an exception. — *end note*]

**20** *Postconditions:* `lock.owns_lock()` is `true` and `lock.mutex()` is locked by the calling thread.

**21** *Returns:* `cv_status::timeout` if the absolute timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `abs_time` expired, otherwise `cv_status::no_timeout`.

**22** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).

```cpp
template<class Rep, class Period>
  cv_status wait_for(unique_lock<mutex>& lock,
                     const chrono::duration<Rep, Period>& rel_time);
```

**23** *Preconditions:* `lock.owns_lock()` is `true` and `lock.mutex()` is locked by the calling thread, and either

- no other thread is waiting on this `condition_variable` object or
- `lock.mutex()` returns the same value for each of the `lock` arguments supplied by all concurrently waiting (via `wait`, `wait_for`, or `wait_until`) threads.

**24** *Effects:* Equivalent to:

```cpp
return wait_until(lock, chrono::steady_clock::now() + rel_time);
```

**25** *Returns:* `cv_status::timeout` if the relative timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `rel_time` expired, otherwise `cv_status::no_timeout`.

**26** *Remarks:* If the function fails to meet the postcondition, `terminate()` is called ([[except.terminate]](../except.terminate.md)). [*Note 5*: This can happen if the re-locking of the mutex throws an exception. — *end note*]

**27** *Postconditions:* `lock.owns_lock()` is `true` and `lock.mutex()` is locked by the calling thread.

**28** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).

```cpp
template<class Clock, class Duration, class Predicate>
  bool wait_until(unique_lock<mutex>& lock,
                  const chrono::time_point<Clock, Duration>& abs_time,
                  Predicate pred);
```

**29** *Preconditions:* `lock.owns_lock()` is `true` and `lock.mutex()` is locked by the calling thread, and either

- no other thread is waiting on this `condition_variable` object or
- `lock.mutex()` returns the same value for each of the `lock` arguments supplied by all concurrently waiting (via `wait`, `wait_for`, or `wait_until`) threads.

**30** *Effects:* Equivalent to:

```cpp
while (!pred())
  if (wait_until(lock, abs_time) == cv_status::timeout)
    return pred();
return true;
```

**31** *Remarks:* If the function fails to meet the postcondition, `terminate()` is called ([[except.terminate]](../except.terminate.md)). [*Note 6*: This can happen if the re-locking of the mutex throws an exception. — *end note*]

**32** *Postconditions:* `lock.owns_lock()` is `true` and `lock.mutex()` is locked by the calling thread.

**33** [*Note 7*: The returned value indicates whether the predicate evaluated to `true` regardless of whether the timeout was triggered. — *end note*]

**34** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)) or any exception thrown by `pred`.

```cpp
template<class Rep, class Period, class Predicate>
  bool wait_for(unique_lock<mutex>& lock,
                const chrono::duration<Rep, Period>& rel_time,
                Predicate pred);
```

**35** *Preconditions:* `lock.owns_lock()` is `true` and `lock.mutex()` is locked by the calling thread, and either

- no other thread is waiting on this `condition_variable` object or
- `lock.mutex()` returns the same value for each of the `lock` arguments supplied by all concurrently waiting (via `wait`, `wait_for`, or `wait_until`) threads.

**36** *Effects:* Equivalent to:

```cpp
return wait_until(lock, chrono::steady_clock::now() + rel_time, std::move(pred));
```

**37** [*Note 8*: There is no blocking if `pred()` is initially `true`, even if the timeout has already expired. — *end note*]

**38** *Remarks:* If the function fails to meet the postcondition, `terminate()` is called ([[except.terminate]](../except.terminate.md)). [*Note 9*: This can happen if the re-locking of the mutex throws an exception. — *end note*]

**39** *Postconditions:* `lock.owns_lock()` is `true` and `lock.mutex()` is locked by the calling thread.

**40** [*Note 10*: The returned value indicates whether the predicate evaluates to `true` regardless of whether the timeout was triggered. — *end note*]

**41** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)) or any exception thrown by `pred`.
