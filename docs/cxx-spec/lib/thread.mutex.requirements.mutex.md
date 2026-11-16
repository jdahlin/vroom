# 32.5.3.2 Mutex types [thread.mutex.requirements.mutex]

[Contents](../README.md) · up: [32.5.3 Mutex requirements \[thread.mutex.requirements\]](thread.mutex.requirements.md) · prev: [32.5.3.1 In general \[thread.mutex.requirements.general\]](thread.mutex.requirements.general.md) · next: [32.5.3.2.1 Class `mutex` \[thread.mutex.class\]](thread.mutex.class.md)

**1** The *mutex types* are the standard library types `mutex`, `recursive_mutex`, `timed_mutex`, `recursive_timed_mutex`, `shared_mutex`, and `shared_timed_mutex`. They meet the requirements set out in this subclause. In this description, `m` denotes an object of a mutex type.

**2** The mutex types meet the *Cpp17Lockable* requirements ([[thread.req.lockable.req]](thread.req.lockable.req.md)).

**3** The mutex types meet *Cpp17DefaultConstructible* and *Cpp17Destructible*. If initialization of an object of a mutex type fails, an exception of type `system_error` is thrown. The mutex types are neither copyable nor movable.

**4** The error conditions for error codes, if any, reported by member functions of the mutex types are as follows:

- `resource_unavailable_try_again` — if any native handle type manipulated is not available.
- `operation_not_permitted` — if the thread does not have the privilege to perform the operation.
- `invalid_argument` — if any native handle type manipulated as part of mutex construction is incorrect.

**5** The implementation provides lock and unlock operations, as described below. For purposes of determining the existence of a data race, these behave as atomic operations ([[intro.multithread]](../intro.multithread.md)). The lock and unlock operations on a single mutex appears to occur in a single total order. [*Note 1*: This can be viewed as the modification order ([[intro.multithread]](../intro.multithread.md)) of the mutex. — *end note*] [*Note 2*: Construction and destruction of an object of a mutex type need not be thread-safe; other synchronization should be used to ensure that mutex objects are initialized and visible to other threads. — *end note*]

**6** The expression `m.lock()` is well-formed and has the following semantics:

**7** *Preconditions:* If `m` is of type `mutex`, `timed_mutex`, `shared_mutex`, or `shared_timed_mutex`, the calling thread does not own the mutex.

**8** *Effects:* Blocks the calling thread until ownership of the mutex can be obtained for the calling thread.

**9** *Postconditions:* The calling thread owns the mutex.

**10** *Return type:* `void`.

**11** *Synchronization:* Prior `unlock()` operations on the same object *synchronize with* ([[intro.multithread]](../intro.multithread.md)) this operation.

**12** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**13** *Error conditions:*

- `operation_not_permitted` — if the thread does not have the privilege to perform the operation.
- `resource_deadlock_would_occur` — if the implementation detects that a deadlock would occur.

**14** The expression `m.try_lock()` is well-formed and has the following semantics:

**15** *Preconditions:* If `m` is of type `mutex`, `timed_mutex`, `shared_mutex`, or `shared_timed_mutex`, the calling thread does not own the mutex.

**16** *Effects:* Attempts to obtain ownership of the mutex for the calling thread without blocking. If ownership is not obtained, there is no effect and `try_lock()` immediately returns. An implementation may fail to obtain the lock even if it is not held by any other thread. [*Note 3*: This spurious failure is normally uncommon, but allows interesting implementations based on a simple compare and exchange ([[atomics]](atomics.md)). — *end note*] An implementation should ensure that `try_lock()` does not consistently return `false` in the absence of contending mutex acquisitions.

**17** *Return type:* `bool`.

**18** *Returns:* `true` if ownership of the mutex was obtained for the calling thread, otherwise `false`.

**19** *Synchronization:* If `try_lock()` returns `true`, prior `unlock()` operations on the same object *synchronize with* ([[intro.multithread]](../intro.multithread.md)) this operation. [*Note 4*: Since `lock()` does not synchronize with a failed subsequent `try_lock()`, the visibility rules are weak enough that little would be known about the state after a failure, even in the absence of spurious failures. — *end note*]

**20** *Throws:* Nothing.

**21** The expression `m.unlock()` is well-formed and has the following semantics:

**22** *Preconditions:* The calling thread owns the mutex.

**23** *Effects:* Releases the calling thread’s ownership of the mutex.

**24** *Return type:* `void`.

**25** *Synchronization:* This operation synchronizes with ([[intro.multithread]](../intro.multithread.md)) subsequent lock operations that obtain ownership on the same object.

**26** *Throws:* Nothing.

## Subclauses

- [32.5.3.2.1 Class `mutex` \[thread.mutex.class\]](thread.mutex.class.md)
- [32.5.3.2.2 Class `recursive_mutex` \[thread.mutex.recursive\]](thread.mutex.recursive.md)
