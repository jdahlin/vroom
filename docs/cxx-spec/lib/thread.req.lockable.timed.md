# 32.2.5.4 *Cpp17TimedLockable* requirements [thread.req.lockable.timed]

[Contents](../README.md) · up: [32.2.5 Requirements for *Cpp17Lockable* types \[thread.req.lockable\]](thread.req.lockable.md) · prev: [32.2.5.3 *Cpp17Lockable* requirements \[thread.req.lockable.req\]](thread.req.lockable.req.md) · next: [32.3 Stop tokens \[thread.stoptoken\]](thread.stoptoken.md)

**1** A type `L` meets the *Cpp17TimedLockable* requirements if it meets the *Cpp17Lockable* requirements and the following expressions are well-formed and have the specified semantics (`m` denotes a value of type `L`, `rel_time` denotes a value of an instantiation of `duration` ([[time.duration]](time.duration.md)), and `abs_time` denotes a value of an instantiation of `time_point` ([[time.point]](time.point.md))).

```cpp
m.try_lock_for(rel_time)
```

**2** *Effects:* Attempts to acquire a lock for the current execution agent within the relative timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `rel_time`. The function will not return within the timeout specified by `rel_time` unless it has obtained a lock on `m` for the current execution agent. If an exception is thrown then a lock has not been acquired for the current execution agent.

**3** *Return type:* `bool`.

**4** *Returns:* `true` if the lock was acquired, `false` otherwise.

```cpp
m.try_lock_until(abs_time)
```

**5** *Effects:* Attempts to acquire a lock for the current execution agent before the absolute timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `abs_time`. The function will not return before the timeout specified by `abs_time` unless it has obtained a lock on `m` for the current execution agent. If an exception is thrown then a lock has not been acquired for the current execution agent.

**6** *Return type:* `bool`.

**7** *Returns:* `true` if the lock was acquired, `false` otherwise.
