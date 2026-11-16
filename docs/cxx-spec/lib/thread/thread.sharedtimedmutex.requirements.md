# 32.5.3.5 Shared timed mutex types [thread.sharedtimedmutex.requirements]

[Contents](../../README.md) · up: [32.5.3 Mutex requirements \[thread.mutex.requirements\]](thread.mutex.requirements.md) · prev: [32.5.3.4.1 Class `shared_mutex` \[thread.sharedmutex.class\]](thread.sharedmutex.class.md) · next: [32.5.3.5.1 Class `shared_timed_mutex` \[thread.sharedtimedmutex.class\]](thread.sharedtimedmutex.class.md)

**1** The standard library type `shared_timed_mutex` is a *shared timed mutex type*. Shared timed mutex types meet the requirements of timed mutex types ([[thread.timedmutex.requirements]](thread.timedmutex.requirements.md)), shared mutex types ([[thread.sharedmutex.requirements]](thread.sharedmutex.requirements.md)), and additionally meet the requirements set out below. In this description, `m` denotes an object of a shared timed mutex type, `rel_type` denotes an object of an instantiation of `duration` ([[time.duration]](../time/time.duration.md)), and `abs_time` denotes an object of an instantiation of `time_point` ([[time.point]](../time/time.point.md)).

**2** The expression `m.try_lock_shared_for(rel_time)` is well-formed and has the following semantics:

**3** *Preconditions:* The calling thread has no ownership of the mutex.

**4** *Effects:* Attempts to obtain shared lock ownership for the calling thread within the relative timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `rel_time`. If the time specified by `rel_time` is less than or equal to `rel_time.zero()`, the function attempts to obtain ownership without blocking (as if by calling `try_lock_shared()`). The function returns within the timeout specified by `rel_time` only if it has obtained shared ownership of the mutex object. [*Note 1*: As with `try_lock()`, there is no guarantee that ownership will be obtained if the lock is available, but implementations are expected to make a strong effort to do so. — *end note*] If an exception is thrown then a shared lock has not been acquired for the current thread.

**5** *Return type:* `bool`.

**6** *Returns:* `true` if the shared lock was acquired, `false` otherwise.

**7** *Synchronization:* If `try_lock_shared_for()` returns `true`, prior `unlock()` operations on the same object synchronize with ([[intro.multithread]](../../basic/intro.multithread.md)) this operation.

**8** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).

**9** The expression `m.try_lock_shared_until(abs_time)` is well-formed and has the following semantics:

**10** *Preconditions:* The calling thread has no ownership of the mutex.

**11** *Effects:* The function attempts to obtain shared ownership of the mutex. If `abs_time` has already passed, the function attempts to obtain shared ownership without blocking (as if by calling `try_lock_shared()`). The function returns before the absolute timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `abs_time` only if it has obtained shared ownership of the mutex object. [*Note 2*: As with `try_lock()`, there is no guarantee that ownership will be obtained if the lock is available, but implementations are expected to make a strong effort to do so. — *end note*] If an exception is thrown then a shared lock has not been acquired for the current thread.

**12** *Return type:* `bool`.

**13** *Returns:* `true` if the shared lock was acquired, `false` otherwise.

**14** *Synchronization:* If `try_lock_shared_until()` returns `true`, prior `unlock()` operations on the same object synchronize with ([[intro.multithread]](../../basic/intro.multithread.md)) this operation.

**15** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).

## Subclauses

- [32.5.3.5.1 Class `shared_timed_mutex` \[thread.sharedtimedmutex.class\]](thread.sharedtimedmutex.class.md)
