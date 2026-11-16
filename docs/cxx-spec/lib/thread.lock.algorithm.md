# 32.5.5 Generic locking algorithms [thread.lock.algorithm]

[Contents](../README.md) · up: [32.5 Mutual exclusion \[thread.mutex\]](thread.mutex.md) · prev: [32.5.4.4.4 Observers \[thread.lock.shared.obs\]](thread.lock.shared.obs.md) · next: [32.5.6 Call once \[thread.once\]](thread.once.md)

```cpp
template<class L1, class L2, class... L3> int try_lock(L1&, L2&, L3&...);
```

**1** *Preconditions:* Each template parameter type meets the *Cpp17Lockable* requirements. [*Note 1*: The `unique_lock` class template meets these requirements when suitably instantiated. — *end note*]

**2** *Effects:* Calls `try_lock()` for each argument in order beginning with the first until all arguments have been processed or a call to `try_lock()` fails, either by returning `false` or by throwing an exception. If a call to `try_lock()` fails, `unlock()` is called for all prior arguments with no further calls to `try_lock()`.

**3** *Returns:* `-1` if all calls to `try_lock()` returned `true`, otherwise a zero-based index value that indicates the argument for which `try_lock()` returned `false`.

```cpp
template<class L1, class L2, class... L3> void lock(L1&, L2&, L3&...);
```

**4** *Preconditions:* Each template parameter type meets the *Cpp17Lockable* requirements. [*Note 2*: The `unique_lock` class template meets these requirements when suitably instantiated. — *end note*]

**5** *Effects:* All arguments are locked via a sequence of calls to `lock()`, `try_lock()`, or `unlock()` on each argument. The sequence of calls does not result in deadlock, but is otherwise unspecified. [*Note 3*: A deadlock avoidance algorithm such as try-and-back-off must be used, but the specific algorithm is not specified to avoid over-constraining implementations. — *end note*] If a call to `lock()` or `try_lock()` throws an exception, `unlock()` is called for any argument that had been locked by a call to `lock()` or `try_lock()`.
