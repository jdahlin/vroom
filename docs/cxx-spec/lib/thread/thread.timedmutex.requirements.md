# 32.5.3.3 Timed mutex types [thread.timedmutex.requirements]

[Contents](../../README.md) · up: [32.5.3 Mutex requirements \[thread.mutex.requirements\]](thread.mutex.requirements.md) · prev: [32.5.3.2.2 Class `recursive_mutex` \[thread.mutex.recursive\]](thread.mutex.recursive.md) · next: [32.5.3.3.1 Class `timed_mutex` \[thread.timedmutex.class\]](thread.timedmutex.class.md)

**1** The *timed mutex types* are the standard library types `timed_mutex`, `recursive_timed_mutex`, and `shared_timed_mutex`. They meet the requirements set out below. In this description, `m` denotes an object of a mutex type, `rel_time` denotes an object of an instantiation of `duration` ([[time.duration]](../time/time.duration.md)), and `abs_time` denotes an object of an instantiation of `time_point` ([[time.point]](../time/time.point.md)).

**2** The timed mutex types meet the *Cpp17TimedLockable* requirements ([[thread.req.lockable.timed]](thread.req.lockable.timed.md)).

**3** The expression `m.try_lock_for(rel_time)` is well-formed and has the following semantics:

**4** *Preconditions:* If `m` is of type `timed_mutex` or `shared_timed_mutex`, the calling thread does not own the mutex.

**5** *Effects:* The function attempts to obtain ownership of the mutex within the relative timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `rel_time`. If the time specified by `rel_time` is less than or equal to `rel_time.zero()`, the function attempts to obtain ownership without blocking (as if by calling `try_lock()`). The function returns within the timeout specified by `rel_time` only if it has obtained ownership of the mutex object. [*Note 1*: As with `try_lock()`, there is no guarantee that ownership will be obtained if the lock is available, but implementations are expected to make a strong effort to do so. — *end note*]

**6** *Return type:* `bool`.

**7** *Returns:* `true` if ownership was obtained, otherwise `false`.

**8** *Synchronization:* If `try_lock_for()` returns `true`, prior `unlock()` operations on the same object *synchronize with* ([[intro.multithread]](../../basic/intro.multithread.md)) this operation.

**9** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).

**10** The expression `m.try_lock_until(abs_time)` is well-formed and has the following semantics:

**11** *Preconditions:* If `m` is of type `timed_mutex` or `shared_timed_mutex`, the calling thread does not own the mutex.

**12** *Effects:* The function attempts to obtain ownership of the mutex. If `abs_time` has already passed, the function attempts to obtain ownership without blocking (as if by calling `try_lock()`). The function returns before the absolute timeout ([[thread.req.timing]](thread.req.timing.md)) specified by `abs_time` only if it has obtained ownership of the mutex object. [*Note 2*: As with `try_lock()`, there is no guarantee that ownership will be obtained if the lock is available, but implementations are expected to make a strong effort to do so. — *end note*]

**13** *Return type:* `bool`.

**14** *Returns:* `true` if ownership was obtained, otherwise `false`.

**15** *Synchronization:* If `try_lock_until()` returns `true`, prior `unlock()` operations on the same object *synchronize with* ([[intro.multithread]](../../basic/intro.multithread.md)) this operation.

**16** *Throws:* Timeout-related exceptions ([[thread.req.timing]](thread.req.timing.md)).

## Subclauses

- [32.5.3.3.1 Class `timed_mutex` \[thread.timedmutex.class\]](thread.timedmutex.class.md)
- [32.5.3.3.2 Class `recursive_timed_mutex` \[thread.timedmutex.recursive\]](thread.timedmutex.recursive.md)
