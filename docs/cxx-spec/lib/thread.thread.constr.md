# 32.4.2.2 Constructors [thread.thread.constr]

[Contents](../README.md) · up: [32.4.2 Class `thread` \[thread.thread.class\]](thread.thread.class.md) · prev: [32.4.2.1 Class `thread::id` \[thread.thread.id\]](thread.thread.id.md) · next: [32.4.2.3 Destructor \[thread.thread.destr\]](thread.thread.destr.md)

```cpp
thread() noexcept;
```

**1** *Effects:* The object does not represent a thread of execution.

**2** *Postconditions:* `get_id() == id()`.

```cpp
template<class F, class... Args> explicit thread(F&& f, Args&&... args);
```

**3** *Constraints:* `remove_cvref_t<F>` is not the same type as `thread`.

**4** *Mandates:* The following are all `true`:

- `is_constructible_v<decay_t<F>, F>`,
- `(is_constructible_v<decay_t<Args>, Args> && ...)`,
- `is_move_constructible_v<decay_t<F>>`,
- `(is_move_constructible_v<decay_t<Args>> && ...)`, and
- `is_invocable_v<decay_t<F>, decay_t<Args>...>`.

**5** *Preconditions:* `decay_t<F>` and each type in `decay_t<Args>` meet the *Cpp17MoveConstructible* requirements.

**6** *Effects:* The new thread of execution executes

```cpp
invoke(decay-copy(std::forward<F>(f)), decay-copy(std::forward<Args>(args))...)
```

with the calls to `decay-copy` being evaluated in the constructing thread. Any return value from this invocation is ignored. [*Note 1*: This implies that any exceptions not thrown from the invocation of the copy of `f` will be thrown in the constructing thread, not the new thread. — *end note*] If the invocation of `invoke` terminates with an uncaught exception, `terminate` is called.

**7** *Synchronization:* The completion of the invocation of the constructor synchronizes with the beginning of the invocation of the copy of `f`.

**8** *Postconditions:* `get_id() != id()`. `*this` represents the newly started thread.

**9** *Throws:* `system_error` if unable to start the new thread.

**10** *Error conditions:*

- `resource_unavailable_try_again` — the system lacked the necessary resources to create another thread, or the system-imposed limit on the number of threads in a process would be exceeded.

```cpp
thread(thread&& x) noexcept;
```

**11** *Postconditions:* `x.get_id() == id()` and `get_id()` returns the value of `x.get_id()` prior to the start of construction.
