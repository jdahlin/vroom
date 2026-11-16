# 32.2.5.3 *Cpp17Lockable* requirements [thread.req.lockable.req]

[Contents](../../README.md) · up: [32.2.5 Requirements for *Cpp17Lockable* types \[thread.req.lockable\]](thread.req.lockable.md) · prev: [32.2.5.2 *Cpp17BasicLockable* requirements \[thread.req.lockable.basic\]](thread.req.lockable.basic.md) · next: [32.2.5.4 *Cpp17TimedLockable* requirements \[thread.req.lockable.timed\]](thread.req.lockable.timed.md)

**1** A type `L` meets the *Cpp17Lockable* requirements if it meets the *Cpp17BasicLockable* requirements and the following expressions are well-formed and have the specified semantics (`m` denotes a value of type `L`).

```cpp
m.try_lock()
```

**2** *Effects:* Attempts to acquire a lock for the current execution agent without blocking. If an exception is thrown then a lock shall not have been acquired for the current execution agent.

**3** *Return type:* `bool`.

**4** *Returns:* `true` if the lock was acquired, `false` otherwise.
