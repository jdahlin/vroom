# 32.4.2.5 Members [thread.thread.member]

[Contents](../../README.md) · up: [32.4.2 Class `thread` \[thread.thread.class\]](thread.thread.class.md) · prev: [32.4.2.4 Assignment \[thread.thread.assign\]](thread.thread.assign.md) · next: [32.4.2.6 Static members \[thread.thread.static\]](thread.thread.static.md)

```cpp
void swap(thread& x) noexcept;
```

**1** *Effects:* Swaps the state of `*this` and `x`.

```cpp
bool joinable() const noexcept;
```

**2** *Returns:* `get_id() != id()`.

```cpp
void join();
```

**3** *Effects:* Blocks until the thread represented by `*this` has completed.

**4** *Synchronization:* The completion of the thread represented by `*this` synchronizes with ([[intro.multithread]](../../basic/intro.multithread.md)) the corresponding successful `join()` return. [*Note 1*: Operations on `*this` are not synchronized. — *end note*]

**5** *Postconditions:* The thread represented by `*this` has completed. `get_id() == id()`.

**6** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**7** *Error conditions:*

- `resource_deadlock_would_occur` — if deadlock is detected or `get_id() == this_thread::get_id()`.
- `no_such_process` — if the thread is not valid.
- `invalid_argument` — if the thread is not joinable.

```cpp
void detach();
```

**8** *Effects:* The thread represented by `*this` continues execution without the calling thread blocking. When `detach()` returns, `*this` no longer represents the possibly continuing thread of execution. When the thread previously represented by `*this` ends execution, the implementation releases any owned resources.

**9** *Postconditions:* `get_id() == id()`.

**10** *Throws:* `system_error` when an exception is required ([[thread.req.exception]](thread.req.exception.md)).

**11** *Error conditions:*

- `no_such_process` — if the thread is not valid.
- `invalid_argument` — if the thread is not joinable.

```cpp
id get_id() const noexcept;
```

**12** *Returns:* A default constructed `id` object if `*this` does not represent a thread, otherwise `this_thread::get_id()` for the thread of execution represented by `*this`.
