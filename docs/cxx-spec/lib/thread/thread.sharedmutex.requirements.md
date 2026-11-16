# 32.5.3.4 Shared mutex types [thread.sharedmutex.requirements]

[Contents](../../README.md) · up: [32.5.3 Mutex requirements \[thread.mutex.requirements\]](thread.mutex.requirements.md) · prev: [32.5.3.3.2 Class `recursive_timed_mutex` \[thread.timedmutex.recursive\]](thread.timedmutex.recursive.md) · next: [32.5.3.4.1 Class `shared_mutex` \[thread.sharedmutex.class\]](thread.sharedmutex.class.md)

**1** The standard library types `shared_mutex` and `shared_timed_mutex` are *shared mutex types*. Shared mutex types meet the requirements of mutex types ([[thread.mutex.requirements.mutex]](thread.mutex.requirements.mutex.md)) and additionally meet the requirements set out below. In this description, `m` denotes an object of a shared mutex type.

**2** In addition to the exclusive lock ownership mode specified in [[thread.mutex.requirements.mutex]](thread.mutex.requirements.mutex.md), shared mutex types provide a *shared lock* ownership mode. Multiple execution agents can simultaneously hold a shared lock ownership of a shared mutex type. But no execution agent holds a shared lock while another execution agent holds an exclusive lock on the same shared mutex type, and vice-versa. The maximum number of execution agents which can share a shared lock on a single shared mutex type is unspecified, but is at least 10000. If more than the maximum number of execution agents attempt to obtain a shared lock, the excess execution agents block until the number of shared locks are reduced below the maximum amount by other execution agents releasing their shared lock.

**3** The expression `m.lock_shared()` is well-formed and has the following semantics:

**4** *Preconditions:* The calling thread has no ownership of the mutex.

**5** *Effects:* Blocks the calling thread until shared ownership of the mutex can be obtained for the calling thread. If an exception is thrown then a shared lock has not been acquired for the current thread.

**6** *Postconditions:* The calling thread has a shared lock on the mutex.

**7** *Return type:* `void`.

**8** *Synchronization:* Prior `unlock()` operations on the same object synchronize with ([[intro.multithread]](../../basic/intro.multithread.md)) this operation.

**9** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**10** *Error conditions:*

- `operation_not_permitted` — if the thread does not have the privilege to perform the operation.
- `resource_deadlock_would_occur` — if the implementation detects that a deadlock would occur.

**11** The expression `m.unlock_shared()` is well-formed and has the following semantics:

**12** *Preconditions:* The calling thread holds a shared lock on the mutex.

**13** *Effects:* Releases a shared lock on the mutex held by the calling thread.

**14** *Return type:* `void`.

**15** *Synchronization:* This operation synchronizes with ([[intro.multithread]](../../basic/intro.multithread.md)) subsequent `lock()` operations that obtain ownership on the same object.

**16** *Throws:* Nothing.

**17** The expression `m.try_lock_shared()` is well-formed and has the following semantics:

**18** *Preconditions:* The calling thread has no ownership of the mutex.

**19** *Effects:* Attempts to obtain shared ownership of the mutex for the calling thread without blocking. If shared ownership is not obtained, there is no effect and `try_lock_shared()` immediately returns. An implementation may fail to obtain the lock even if it is not held by any other thread.

**20** *Return type:* `bool`.

**21** *Returns:* `true` if the shared ownership lock was acquired, `false` otherwise.

**22** *Synchronization:* If `try_lock_shared()` returns `true`, prior `unlock()` operations on the same object synchronize with ([[intro.multithread]](../../basic/intro.multithread.md)) this operation.

**23** *Throws:* Nothing.

## Subclauses

- [32.5.3.4.1 Class `shared_mutex` \[thread.sharedmutex.class\]](thread.sharedmutex.class.md)
