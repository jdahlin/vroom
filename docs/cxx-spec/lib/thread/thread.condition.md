# 32.6 Condition variables [thread.condition]

[Contents](../../README.md) · up: [32 Thread support library \[thread\]](thread.md) · prev: [32.5.6.2 Function `call_once` \[thread.once.callonce\]](thread.once.callonce.md) · next: [32.6.1 Header `<condition_variable>` synopsis \[condition.variable.syn\]](condition.variable.syn.md)

**1** Condition variables provide synchronization primitives used to block a thread until notified by some other thread that some condition is met or until a system time is reached. Class `condition_variable` provides a condition variable that can only wait on an object of type `unique_lock<mutex>`, allowing the implementation to be more efficient. Class `condition_variable_any` provides a general condition variable that can wait on objects of user-supplied lock types.

**2** Condition variables permit concurrent invocation of the `wait`, `wait_for`, `wait_until`, `notify_one` and `notify_all` member functions.

**3** The executions of `notify_one` and `notify_all` are atomic. The executions of `wait`, `wait_for`, and `wait_until` are performed in three atomic parts:

1. the release of the mutex and entry into the waiting state;
2. the unblocking of the wait; and
3. the reacquisition of the lock.

**4** The implementation behaves as if all executions of `notify_one`, `notify_all`, and each part of the `wait`, `wait_for`, and `wait_until` executions are executed in a single unspecified total order consistent with the "happens before" order.

**5** Condition variable construction and destruction need not be synchronized.

## Subclauses

- [32.6.1 Header `<condition_variable>` synopsis \[condition.variable.syn\]](condition.variable.syn.md)
- [32.6.2 Non-member functions \[thread.condition.nonmember\]](thread.condition.nonmember.md)
- [32.6.3 Class `condition_variable` \[thread.condition.condvar\]](thread.condition.condvar.md)
- [32.6.4 Class `condition_variable_any` \[thread.condition.condvarany\]](thread.condition.condvarany.md)
  - [32.6.4.1 Noninterruptible waits \[thread.condvarany.wait\]](thread.condvarany.wait.md)
  - [32.6.4.2 Interruptible waits \[thread.condvarany.intwait\]](thread.condvarany.intwait.md)
