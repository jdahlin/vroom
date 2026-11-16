# 32.6.2 Non-member functions [thread.condition.nonmember]

[Contents](../README.md) · up: [32.6 Condition variables \[thread.condition\]](thread.condition.md) · prev: [32.6.1 Header `<condition_variable>` synopsis \[condition.variable.syn\]](condition.variable.syn.md) · next: [32.6.3 Class `condition_variable` \[thread.condition.condvar\]](thread.condition.condvar.md)

```cpp
void notify_all_at_thread_exit(condition_variable& cond, unique_lock<mutex> lk);
```

**1** *Preconditions:* `lk` is locked by the calling thread and either

- no other thread is waiting on `cond`, or
- `lk.mutex()` returns the same value for each of the lock arguments supplied by all concurrently waiting (via `wait`, `wait_for`, or `wait_until`) threads.

**2** *Effects:* Transfers ownership of the lock associated with `lk` into internal storage and schedules `cond` to be notified when the current thread exits, after all objects of thread storage duration associated with the current thread have been destroyed. This notification is equivalent to:

```cpp
lk.unlock();
cond.notify_all();
```

**3** *Synchronization:* The implied `lk.unlock()` call is sequenced after the destruction of all objects with thread storage duration associated with the current thread.

**4** [*Note 1*: The supplied lock will be held until the thread exits, and care should be taken to ensure that this does not cause deadlock due to lock ordering issues. After calling `notify_all_at_thread_exit` it is recommended that the thread should be exited as soon as possible, and that no blocking or time-consuming tasks are run on that thread. — *end note*]

**5** [*Note 2*: It is the user’s responsibility to ensure that waiting threads do not erroneously assume that the thread has finished if they experience spurious wakeups. This typically requires that the condition being waited for is satisfied while holding the lock on `lk`, and that this lock is not released and reacquired prior to calling `notify_all_at_thread_exit`. — *end note*]
