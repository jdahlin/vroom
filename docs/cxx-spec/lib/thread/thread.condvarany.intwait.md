# 32.6.4.2 Interruptible waits [thread.condvarany.intwait]

[Contents](../../README.md) · up: [32.6.4 Class `condition_variable_any` \[thread.condition.condvarany\]](thread.condition.condvarany.md) · prev: [32.6.4.1 Noninterruptible waits \[thread.condvarany.wait\]](thread.condvarany.wait.md) · next: [32.7 Semaphore \[thread.sema\]](thread.sema.md)

**1** The following wait functions will be notified when there is a stop request on the passed `stop_token`. In that case the functions return immediately, returning `false` if the predicate evaluates to `false`.

```cpp
template<class Lock, class Predicate>
  bool wait(Lock& lock, stop_token stoken, Predicate pred);
```

**2** *Effects:* Registers for the duration of this call `*this` to get notified on a stop request on `stoken` during this call and then equivalent to:

```cpp
while (!stoken.stop_requested()) {
  if (pred())
    return true;
  wait(lock);
}
return pred();
```

**3** [*Note 1*: The returned value indicates whether the predicate evaluated to `true` regardless of whether there was a stop request. — *end note*]

**4** *Postconditions:* `lock` is locked by the calling thread.

**5** *Remarks:* If the function fails to meet the postcondition, `terminate` is called ([[except.terminate]](../../except/except.terminate.md)). [*Note 2*: This can happen if the re-locking of the mutex throws an exception. — *end note*]

**6** *Throws:* Any exception thrown by `pred`.

```cpp
template<class Lock, class Clock, class Duration, class Predicate>
  bool wait_until(Lock& lock, stop_token stoken,
                  const chrono::time_point<Clock, Duration>& abs_time, Predicate pred);
```

**7** *Effects:* Registers for the duration of this call `*this` to get notified on a stop request on `stoken` during this call and then equivalent to:

```cpp
while (!stoken.stop_requested()) {
  if (pred())
    return true;
  if (cv.wait_until(lock, abs_time) == cv_status::timeout)
    return pred();
}
return pred();
```

**8** [*Note 3*: There is no blocking if `pred()` is initially `true`, `stoken.stop_requested()` was already `true` or the timeout has already expired. — *end note*]

**9** [*Note 4*: The returned value indicates whether the predicate evaluated to `true` regardless of whether the timeout was triggered or a stop request was made. — *end note*]

**10** *Postconditions:* `lock` is locked by the calling thread.

**11** *Remarks:* If the function fails to meet the postcondition, `terminate` is called ([[except.terminate]](../../except/except.terminate.md)). [*Note 5*: This can happen if the re-locking of the mutex throws an exception. — *end note*]

**12** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)), or any exception thrown by `pred`.

```cpp
template<class Lock, class Rep, class Period, class Predicate>
  bool wait_for(Lock& lock, stop_token stoken,
                const chrono::duration<Rep, Period>& rel_time, Predicate pred);
```

**13** *Effects:* Equivalent to:

```cpp
return wait_until(lock, std::move(stoken), chrono::steady_clock::now() + rel_time,
                  std::move(pred));
```
