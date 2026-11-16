# 32.6.4.1 Noninterruptible waits [thread.condvarany.wait]

[Contents](../README.md) · up: [32.6.4 Class `condition_variable_any` \[thread.condition.condvarany\]](thread.condition.condvarany.md) · prev: [32.6.4 Class `condition_variable_any` \[thread.condition.condvarany\]](thread.condition.condvarany.md) · next: [32.6.4.2 Interruptible waits \[thread.condvarany.intwait\]](thread.condvarany.intwait.md)

```cpp
template<class Lock>
  void wait(Lock& lock);
```

**1** *Effects:*

- Atomically calls `lock.unlock()` and blocks on `*this`.
- When unblocked, calls `lock.lock()` (possibly blocking on the lock) and returns.
- The function will unblock when signaled by a call to `notify_one()`, a call to `notify_all()`, or spuriously.

**2** *Remarks:* If the function fails to meet the postcondition, `terminate()` is called ([[except.terminate]](../except.terminate.md)). [*Note 1*: This can happen if the re-locking of the mutex throws an exception. — *end note*]

**3** *Postconditions:* `lock` is locked by the calling thread.

**4** *Throws:* Nothing.

```cpp
template<class Lock, class Predicate>
  void wait(Lock& lock, Predicate pred);
```

**5** *Effects:* Equivalent to:

```cpp
while (!pred())
  wait(lock);
```

```cpp
template<class Lock, class Clock, class Duration>
  cv_status wait_until(Lock& lock, const chrono::time_point<Clock, Duration>& abs_time);
```

**6** *Effects:*

- Atomically calls `lock.unlock()` and blocks on `*this`.
- When unblocked, calls `lock.lock()` (possibly blocking on the lock) and returns.
- The function will unblock when signaled by a call to `notify_one()`, a call to `notify_all()`, expiration of the absolute timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `abs_time`, or spuriously.
- If the function exits via an exception, `lock.lock()` is called prior to exiting the function.

**7** *Remarks:* If the function fails to meet the postcondition, `terminate()` is called ([[except.terminate]](../except.terminate.md)). [*Note 2*: This can happen if the re-locking of the mutex throws an exception. — *end note*]

**8** *Postconditions:* `lock` is locked by the calling thread.

**9** *Returns:* `cv_status::timeout` if the absolute timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `abs_time` expired, otherwise `cv_status::no_timeout`.

**10** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).

```cpp
template<class Lock, class Rep, class Period>
  cv_status wait_for(Lock& lock, const chrono::duration<Rep, Period>& rel_time);
```

**11** *Effects:* Equivalent to:

```cpp
return wait_until(lock, chrono::steady_clock::now() + rel_time);
```

**12** *Returns:* `cv_status::timeout` if the relative timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `rel_time` expired, otherwise `cv_status::no_timeout`.

**13** *Remarks:* If the function fails to meet the postcondition, `terminate()` is called ([[except.terminate]](../except.terminate.md)). [*Note 3*: This can happen if the re-locking of the mutex throws an exception. — *end note*]

**14** *Postconditions:* `lock` is locked by the calling thread.

**15** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).

```cpp
template<class Lock, class Clock, class Duration, class Predicate>
  bool wait_until(Lock& lock, const chrono::time_point<Clock, Duration>& abs_time, Predicate pred);
```

**16** *Effects:* Equivalent to:

```cpp
while (!pred())
  if (wait_until(lock, abs_time) == cv_status::timeout)
    return pred();
return true;
```

**17** [*Note 4*: There is no blocking if `pred()` is initially `true`, or if the timeout has already expired. — *end note*]

**18** [*Note 5*: The returned value indicates whether the predicate evaluates to `true` regardless of whether the timeout was triggered. — *end note*]

```cpp
template<class Lock, class Rep, class Period, class Predicate>
  bool wait_for(Lock& lock, const chrono::duration<Rep, Period>& rel_time, Predicate pred);
```

**19** *Effects:* Equivalent to:

```cpp
return wait_until(lock, chrono::steady_clock::now() + rel_time, std::move(pred));
```
