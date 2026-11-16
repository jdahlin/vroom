# 32.2.5.2 *Cpp17BasicLockable* requirements [thread.req.lockable.basic]

[Contents](../README.md) · up: [32.2.5 Requirements for *Cpp17Lockable* types \[thread.req.lockable\]](thread.req.lockable.md) · prev: [32.2.5.1 In general \[thread.req.lockable.general\]](thread.req.lockable.general.md) · next: [32.2.5.3 *Cpp17Lockable* requirements \[thread.req.lockable.req\]](thread.req.lockable.req.md)

**1** A type `L` meets the *Cpp17BasicLockable* requirements if the following expressions are well-formed and have the specified semantics (`m` denotes a value of type `L`).

```cpp
m.lock()
```

**2** *Effects:* Blocks until a lock can be acquired for the current execution agent. If an exception is thrown then a lock shall not have been acquired for the current execution agent.

```cpp
m.unlock()
```

**3** *Preconditions:* The current execution agent holds a lock on `m`.

**4** *Effects:* Releases a lock on `m` held by the current execution agent.

**5** *Throws:* Nothing.
