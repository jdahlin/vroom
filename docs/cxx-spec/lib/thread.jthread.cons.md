# 32.4.3.1 Constructors, move, and assignment [thread.jthread.cons]

[Contents](../README.md) · up: [32.4.3 Class `jthread` \[thread.jthread.class\]](thread.jthread.class.md) · prev: [32.4.3 Class `jthread` \[thread.jthread.class\]](thread.jthread.class.md) · next: [32.4.3.2 Members \[thread.jthread.mem\]](thread.jthread.mem.md)

```cpp
jthread() noexcept;
```

**1** *Effects:* Constructs a `jthread` object that does not represent a thread of execution.

**2** *Postconditions:* `get_id() == id()` is `true` and `ssource.stop_possible()` is `false`.

```cpp
template<class F, class... Args> explicit jthread(F&& f, Args&&... args);
```

**3** *Constraints:* `remove_cvref_t<F>` is not the same type as `jthread`.

**4** *Mandates:* The following are all `true`:

- `is_constructible_v<decay_t<F>, F>`,
- `(is_constructible_v<decay_t<Args>, Args> && ...)`,
- `is_move_constructible_v<decay_t<F>>`,
- `(is_move_constructible_v<decay_t<Args>> && ...)`, and
- `is_invocable_v<decay_t<F>, decay_t<Args>...> ||` <br> `is_invocable_v<decay_t<F>, stop_token, decay_t<Args>...>`.

**5** *Preconditions:* `decay_t<F>` and each type in `decay_t<Args>` meet the *Cpp17MoveConstructible* requirements.

**6** *Effects:* Initializes `ssource`. The new thread of execution executes

```cpp
invoke(decay-copy(std::forward<F>(f)), get_stop_token(),
       decay-copy(std::forward<Args>(args))...)
```

if that expression is well-formed, otherwise

```cpp
invoke(decay-copy(std::forward<F>(f)), decay-copy(std::forward<Args>(args))...)
```

with the calls to `decay-copy` being evaluated in the constructing thread. Any return value from this invocation is ignored. [*Note 1*: This implies that any exceptions not thrown from the invocation of the copy of `f` will be thrown in the constructing thread, not the new thread. — *end note*] If the `invoke` expression exits via an exception, `terminate` is called.

**7** *Synchronization:* The completion of the invocation of the constructor synchronizes with the beginning of the invocation of the copy of `f`.

**8** *Postconditions:* `get_id() != id()` is `true` and `ssource.stop_possible()` is `true` and `*this` represents the newly started thread. [*Note 2*: The calling thread can make a stop request only once, because it cannot replace this stop token. — *end note*]

**9** *Throws:* `system_error` if unable to start the new thread.

**10** *Error conditions:*

- `resource_unavailable_try_again` — the system lacked the necessary resources to create another thread, or the system-imposed limit on the number of threads in a process would be exceeded.

```cpp
jthread(jthread&& x) noexcept;
```

**11** *Postconditions:* `x.get_id() == id()` and `get_id()` returns the value of `x.get_id()` prior to the start of construction. `ssource` has the value of `x.ssource` prior to the start of construction and `x.ssource.stop_possible()` is `false`.

```cpp
~jthread();
```

**12** *Effects:* If `joinable()` is `true`, calls `request_stop()` and then `join()`. [*Note 3*: Operations on `*this` are not synchronized. — *end note*]

```cpp
jthread& operator=(jthread&& x) noexcept;
```

**13** *Effects:* If `joinable()` is `true`, calls `request_stop()` and then `join()`. Assigns the state of `x` to `*this` and sets `x` to a default constructed state.

**14** *Postconditions:* `x.get_id() == id()` and `get_id()` returns the value of `x.get_id()` prior to the assignment. `ssource` has the value of `x.ssource` prior to the assignment and `x.ssource.stop_possible()` is `false`.

**15** *Returns:* `*this`.
