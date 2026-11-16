# 32.6.4 Class `condition_variable_any` [thread.condition.condvarany]

[Contents](../../README.md) · up: [32.6 Condition variables \[thread.condition\]](thread.condition.md) · prev: [32.6.3 Class `condition_variable` \[thread.condition.condvar\]](thread.condition.condvar.md) · next: [32.6.4.1 Noninterruptible waits \[thread.condvarany.wait\]](thread.condvarany.wait.md)

**1** A `Lock` type shall meet the *Cpp17BasicLockable* requirements ([[thread.req.lockable.basic]](thread.req.lockable.basic.md)). [*Note 1*: All of the standard mutex types meet this requirement. If a `Lock` type other than one of the standard mutex types or a `unique_lock` wrapper for a standard mutex type is used with `condition_variable_any`, the user should ensure that any necessary synchronization is in place with respect to the predicate associated with the `condition_variable_any` instance. — *end note*]

```cpp
namespace std {
  class condition_variable_any {
  public:
    condition_variable_any();
    ~condition_variable_any();

    condition_variable_any(const condition_variable_any&) = delete;
    condition_variable_any& operator=(const condition_variable_any&) = delete;

    void notify_one() noexcept;
    void notify_all() noexcept;

    // [thread.condvarany.wait], noninterruptible waits
    template<class Lock>
      void wait(Lock& lock);
    template<class Lock, class Predicate>
      void wait(Lock& lock, Predicate pred);

    template<class Lock, class Clock, class Duration>
      cv_status wait_until(Lock& lock, const chrono::time_point<Clock, Duration>& abs_time);
    template<class Lock, class Clock, class Duration, class Predicate>
      bool wait_until(Lock& lock, const chrono::time_point<Clock, Duration>& abs_time,
                      Predicate pred);
    template<class Lock, class Rep, class Period>
      cv_status wait_for(Lock& lock, const chrono::duration<Rep, Period>& rel_time);
    template<class Lock, class Rep, class Period, class Predicate>
      bool wait_for(Lock& lock, const chrono::duration<Rep, Period>& rel_time, Predicate pred);

    // [thread.condvarany.intwait], interruptible waits
    template<class Lock, class Predicate>
      bool wait(Lock& lock, stop_token stoken, Predicate pred);
    template<class Lock, class Clock, class Duration, class Predicate>
      bool wait_until(Lock& lock, stop_token stoken,
                      const chrono::time_point<Clock, Duration>& abs_time, Predicate pred);
    template<class Lock, class Rep, class Period, class Predicate>
      bool wait_for(Lock& lock, stop_token stoken,
                    const chrono::duration<Rep, Period>& rel_time, Predicate pred);
  };
}
```

```cpp
condition_variable_any();
```

**2** *Throws:* `bad_alloc` or `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**3** *Error conditions:*

- `resource_unavailable_try_again` — if some non-memory resource limitation prevents initialization.
- `operation_not_permitted` — if the thread does not have the privilege to perform the operation.

```cpp
~condition_variable_any();
```

**4** *Preconditions:* There is no thread blocked on `*this`. [*Note 2*: That is, all threads have been notified; they could subsequently block on the lock specified in the wait. This relaxes the usual rules, which would have required all wait calls to happen before destruction. Only the notification to unblock the wait needs to happen before destruction. The user should take care to ensure that no threads wait on `*this` once the destructor has been started, especially when the waiting threads are calling the wait functions in a loop or using the overloads of `wait`, `wait_for`, or `wait_until` that take a predicate. — *end note*]

```cpp
void notify_one() noexcept;
```

**5** *Effects:* If any threads are blocked waiting for `*this`, unblocks one of those threads.

```cpp
void notify_all() noexcept;
```

**6** *Effects:* Unblocks all threads that are blocked waiting for `*this`.

## Subclauses

- [32.6.4.1 Noninterruptible waits \[thread.condvarany.wait\]](thread.condvarany.wait.md)
- [32.6.4.2 Interruptible waits \[thread.condvarany.intwait\]](thread.condvarany.intwait.md)
