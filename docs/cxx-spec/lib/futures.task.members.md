# 32.9.10.1 Member functions [futures.task.members]

[Contents](../README.md) · up: [32.9.10 Class template `packaged_task` \[futures.task\]](futures.task.md) · prev: [32.9.10 Class template `packaged_task` \[futures.task\]](futures.task.md) · next: [32.9.10.2 Globals \[futures.task.nonmembers\]](futures.task.nonmembers.md)

```cpp
packaged_task() noexcept;
```

**1** *Effects:* The object has no shared state and no stored task.

```cpp
template<class F>
  packaged_task(F&& f);
```

**2** *Constraints:* `remove_cvref_t<F>` is not the same type as `packaged_task<R(ArgTypes...)>`.

**3** *Mandates:* `is_invocable_r_v<R, F&, ArgTypes...>` is `true`.

**4** *Preconditions:* Invoking a copy of `f` behaves the same as invoking `f`.

**5** *Effects:* Constructs a new `packaged_task` object with a shared state and initializes the object’s stored task with `std::forward<F>(f)`.

**6** *Throws:* Any exceptions thrown by the copy or move constructor of `f`, or `bad_alloc` if memory for the internal data structures could not be allocated.

```cpp
packaged_task(packaged_task&& rhs) noexcept;
```

**7** *Effects:* Transfers ownership of `rhs`’s shared state to `*this`, leaving `rhs` with no shared state. Moves the stored task from `rhs` to `*this`.

**8** *Postconditions:* `rhs` has no shared state.

```cpp
packaged_task& operator=(packaged_task&& rhs) noexcept;
```

**9** *Effects:*

- Releases any shared state ([[futures.state]](futures.state.md));
- calls `packaged_task(std::move(rhs)).swap(*this)`.

```cpp
~packaged_task();
```

**10** *Effects:* Abandons any shared state ([[futures.state]](futures.state.md)).

```cpp
void swap(packaged_task& other) noexcept;
```

**11** *Effects:* Exchanges the shared states and stored tasks of `*this` and `other`.

**12** *Postconditions:* `*this` has the same shared state and stored task (if any) as `other` prior to the call to `swap`. `other` has the same shared state and stored task (if any) as `*this` prior to the call to `swap`.

```cpp
bool valid() const noexcept;
```

**13** *Returns:* `true` only if `*this` has a shared state.

```cpp
future<R> get_future();
```

**14** *Returns:* A `future` object that shares the same shared state as `*this`.

**15** *Synchronization:* Calls to this function do not introduce data races ([[intro.multithread]](../intro.multithread.md)) with calls to `operator()` or `make_ready_at_thread_exit`. [*Note 1*: Such calls need not synchronize with each other. — *end note*]

**16** *Throws:* A `future_error` object if an error occurs.

**17** *Error conditions:*

- `future_already_retrieved` if `get_future` has already been called on a `packaged_task` object with the same shared state as `*this`.
- `no_state` if `*this` has no shared state.

```cpp
void operator()(ArgTypes... args);
```

**18** *Effects:* As if by `INVOKE<R>(f, t_1, t_2, ..., t_N)` ([[func.require]](func.require.md)), where `f` is the stored task of `*this` and `t_1, t_2, ..., t_N` are the values in `args...`. If the task returns normally, the return value is stored as the asynchronous result in the shared state of `*this`, otherwise the exception thrown by the task is stored. The shared state of `*this` is made ready, and any threads blocked in a function waiting for the shared state of `*this` to become ready are unblocked.

**19** *Throws:* A `future_error` exception object if there is no shared state or the stored task has already been invoked.

**20** *Error conditions:*

- `promise_already_satisfied` if the stored task has already been invoked.
- `no_state` if `*this` has no shared state.

```cpp
void make_ready_at_thread_exit(ArgTypes... args);
```

**21** *Effects:* As if by `INVOKE<R>(f, t_1, t_2, ..., t_N)` ([[func.require]](func.require.md)), where `f` is the stored task and `t_1, t_2, ..., t_N` are the values in `args...`. If the task returns normally, the return value is stored as the asynchronous result in the shared state of `*this`, otherwise the exception thrown by the task is stored. In either case, this is done without making that state ready ([[futures.state]](futures.state.md)) immediately. Schedules the shared state to be made ready when the current thread exits, after all objects of thread storage duration associated with the current thread have been destroyed.

**22** *Throws:* `future_error` if an error condition occurs.

**23** *Error conditions:*

- `promise_already_satisfied` if the stored task has already been invoked.
- `no_state` if `*this` has no shared state.

```cpp
void reset();
```

**24** *Effects:* As if `*this = packaged_task(std::move(f))`, where `f` is the task stored in `*this`. [*Note 2*: This constructs a new shared state for `*this`. The old state is abandoned ([[futures.state]](futures.state.md)). — *end note*]

**25** *Throws:*

- `bad_alloc` if memory for the new shared state could not be allocated.
- any exception thrown by the move constructor of the task stored in the shared state.
- `future_error` with an error condition of `no_state` if `*this` has no shared state.
